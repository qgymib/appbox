#include "utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include "RemoteProtocol.hpp"
#include "utils/PipeClient.hpp"

namespace
{

/* Counter which keeps the pipe name of every test unique. */
std::atomic<uint32_t> g_pipe_counter = 0;

/**
 * @brief Build a pipe path which is unique inside the test process.
 * @return The path of a named pipe.
 */
std::wstring MakePipePath()
{
    const uint32_t index = g_pipe_counter.fetch_add(1);
    return L"\\\\.\\pipe\\appbox_unit_pipe_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
           std::to_wstring(index);
}

/**
 * @brief Read exactly the requested number of bytes from a pipe.
 * @param[in] pipe Pipe handle.
 * @param[out] data Destination buffer.
 * @param[in] size Number of bytes to read.
 * @return true when every byte was read.
 */
bool ReadExactly(HANDLE pipe, void* data, size_t size)
{
    size_t done = 0;
    auto   bytes = static_cast<uint8_t*>(data);

    while (done < size)
    {
        DWORD read = 0;
        if (!ReadFile(pipe, bytes + done, static_cast<DWORD>(size - done), &read, nullptr) || read == 0)
        {
            return false;
        }

        done += read;
    }

    return true;
}

/**
 * @brief Write every byte of a buffer into a pipe.
 * @param[in] pipe Pipe handle.
 * @param[in] data Source buffer.
 * @param[in] size Number of bytes to write.
 * @return true when every byte was written.
 */
bool WriteExactly(HANDLE pipe, const void* data, size_t size)
{
    size_t done = 0;
    auto   bytes = static_cast<const uint8_t*>(data);

    while (done < size)
    {
        DWORD written = 0;
        if (!WriteFile(pipe, bytes + done, static_cast<DWORD>(size - done), &written, nullptr) || written == 0)
        {
            return false;
        }

        done += written;
    }

    return true;
}

/**
 * @brief Builder of a canned answer of the fake loader side.
 *
 * The request payload is handed over, the returned text is written back as the
 * payload of the response frame.
 */
using ReplyBuilder = std::function<std::string(const std::string& request)>;

/**
 * @brief Serve exactly one RPC request like the loader does.
 *
 * The pipe instance is created before the client connects, so the client can
 * not race the server. The worker thread reads one request, writes the canned
 * reply and leaves. The destructor unblocks a worker which is still waiting,
 * so a failing assertion can not hang the test run.
 */
struct FakeLoader
{
    std::wstring path;                    /* Pipe path of this instance. */
    HANDLE       pipe = INVALID_HANDLE_VALUE;
    std::thread  worker;

    FakeLoader(const std::wstring& pipe_path, ReplyBuilder reply, bool correct_magic = true) : path(pipe_path)
    {
        pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                4096, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        worker = std::thread([this, reply = std::move(reply), correct_magic]() {
            const BOOL connected = ConnectNamedPipe(pipe, nullptr);
            if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
            {
                return;
            }

            appbox::RemoteProtocol request_header;
            if (!ReadExactly(pipe, &request_header, sizeof(request_header)))
            {
                return;
            }

            std::string request(request_header.length, '\0');
            if (!ReadExactly(pipe, request.data(), request.size()))
            {
                return;
            }

            const std::string payload = reply(request);

            appbox::RemoteProtocol response_header;
            response_header.length = static_cast<uint32_t>(payload.size());
            if (!correct_magic)
            {
                response_header.magic = 0;
            }

            if (!WriteExactly(pipe, &response_header, sizeof(response_header)))
            {
                return;
            }

            WriteExactly(pipe, payload.data(), payload.size());
            FlushFileBuffers(pipe);
        });
    }

    ~FakeLoader()
    {
        if (pipe != INVALID_HANDLE_VALUE)
        {
            /* Unblock a worker which still waits for the request. */
            DisconnectNamedPipe(pipe);
        }

        if (worker.joinable())
        {
            worker.join();
        }

        if (pipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(pipe);
        }
    }

    FakeLoader(const FakeLoader&) = delete;
    FakeLoader& operator=(const FakeLoader&) = delete;
};

/**
 * @brief Answer with the id of the request, optionally with a result.
 * @param[in] request Request payload.
 * @param[in] id_offset Offset added to the id of the request.
 * @param[in] with_result Whether the answer carries a result member.
 * @return The payload of the response.
 */
std::string AnswerTo(const std::string& request, uint64_t id_offset, bool with_result)
{
    const nlohmann::json req = nlohmann::json::parse(request);

    nlohmann::json rsp;
    rsp["jsonrpc"] = "2.0";
    rsp["id"] = req["id"].get<uint64_t>() + id_offset;
    if (with_result)
    {
        rsp["result"] = nlohmann::json::object();
    }

    return rsp.dump();
}

} // namespace

/**
 * @brief A response which is not valid json is a transport failure. The hook
 *        which logs must see the boolean result, never an exception.
 */
TEST(UnitPipeClient, MalformedResponseIsRejected)
{
    FakeLoader loader(MakePipePath(), [](const std::string&) { return std::string("{ not json"); });

    appbox::PipeClient client(loader.path);
    ASSERT_TRUE(client.Start());

    nlohmann::json rsp;
    bool           result = true;
    EXPECT_NO_THROW(result = client.Call("unit.method", nlohmann::json::object(), rsp));
    EXPECT_FALSE(result);
}

/**
 * @brief A response which is valid json but not an object is rejected without
 *        touching its members.
 */
TEST(UnitPipeClient, NonObjectResponseIsRejected)
{
    FakeLoader loader(MakePipePath(), [](const std::string&) { return std::string("[1,2,3]"); });

    appbox::PipeClient client(loader.path);
    ASSERT_TRUE(client.Start());

    nlohmann::json rsp;
    bool           result = true;
    EXPECT_NO_THROW(result = client.Call("unit.method", nlohmann::json::object(), rsp));
    EXPECT_FALSE(result);
}

/**
 * @brief A response which belongs to another request is rejected.
 */
TEST(UnitPipeClient, ResponseWithAForeignIdIsRejected)
{
    FakeLoader loader(MakePipePath(), [](const std::string& request) { return AnswerTo(request, 1, true); });

    appbox::PipeClient client(loader.path);
    ASSERT_TRUE(client.Start());

    nlohmann::json rsp;
    bool           result = true;
    EXPECT_NO_THROW(result = client.Call("unit.method", nlohmann::json::object(), rsp));
    EXPECT_FALSE(result);
}

/**
 * @brief A response without a result member is rejected.
 */
TEST(UnitPipeClient, ResponseWithoutAResultIsRejected)
{
    FakeLoader loader(MakePipePath(), [](const std::string& request) { return AnswerTo(request, 0, false); });

    appbox::PipeClient client(loader.path);
    ASSERT_TRUE(client.Start());

    nlohmann::json rsp;
    bool           result = true;
    EXPECT_NO_THROW(result = client.Call("unit.method", nlohmann::json::object(), rsp));
    EXPECT_FALSE(result);
}

/**
 * @brief A frame with a wrong magic is a transport failure.
 */
TEST(UnitPipeClient, WrongMagicIsRejected)
{
    FakeLoader loader(MakePipePath(), [](const std::string& request) { return AnswerTo(request, 0, true); }, false);

    appbox::PipeClient client(loader.path);
    ASSERT_TRUE(client.Start());

    nlohmann::json rsp;
    bool           result = true;
    EXPECT_NO_THROW(result = client.Call("unit.method", nlohmann::json::object(), rsp));
    EXPECT_FALSE(result);
}

/**
 * @brief A well formed response is delivered to the caller.
 */
TEST(UnitPipeClient, ValidResponseIsDelivered)
{
    FakeLoader loader(MakePipePath(), [](const std::string& request) {
        nlohmann::json req = nlohmann::json::parse(request);

        nlohmann::json rsp;
        rsp["jsonrpc"] = "2.0";
        rsp["id"] = req["id"].get<uint64_t>();
        rsp["result"] = {{"method", req["method"].get<std::string>()}};
        return rsp.dump();
    });

    appbox::PipeClient client(loader.path);
    ASSERT_TRUE(client.Start());

    nlohmann::json rsp;
    bool           result = false;
    EXPECT_NO_THROW(result = client.Call("unit.method", nlohmann::json::object(), rsp));

    EXPECT_TRUE(result);
    EXPECT_EQ(rsp["method"].get<std::string>(), "unit.method");
}
