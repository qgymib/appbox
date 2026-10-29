#include "WinAPI.h" /* Must be first include file */
#include <chrono>
#include <cstdint>
#include <thread>
#include "RpcCodec.hpp"
#include "MultiInstancePipe.hpp"

namespace
{

/** Interval of the poll which waits for the answer of the server, in milliseconds. */
constexpr std::uint32_t kPollIntervalMs = 5;

/** Budget of the connection of the client, in milliseconds. */
constexpr std::uint32_t kConnectTimeoutMs = 30000;

/** Wait of one attempt to connect to a pipe whose instances are all busy. */
constexpr std::uint32_t kConnectWaitMs = 200;

/**
 * @brief Wait until a pipe holds at least the given number of bytes.
 * @param[in] pipe Handle of the client end of the pipe.
 * @param[in] count Number of bytes to wait for.
 * @param[in] deadline Time the wait ends at.
 * @return true when the bytes are available.
 */
bool WaitForBytes(HANDLE pipe, DWORD count, std::chrono::steady_clock::time_point deadline)
{
    for (;;)
    {
        DWORD available = 0;
        if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == FALSE)
        {
            return false;
        }

        if (available >= count)
        {
            return true;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
}

/**
 * @brief Read the given number of bytes from a pipe.
 * @param[in] pipe Handle of the client end of the pipe.
 * @param[out] buffer Buffer to fill.
 * @param[in] size Number of bytes to read.
 * @return true when every byte was read.
 */
bool ReadExact(HANDLE pipe, void* buffer, DWORD size)
{
    auto* cursor = static_cast<std::uint8_t*>(buffer);
    DWORD done = 0;

    while (done < size)
    {
        DWORD read = 0;
        if (ReadFile(pipe, cursor + done, size - done, &read, nullptr) == FALSE || read == 0)
        {
            return false;
        }

        done += read;
    }

    return true;
}

} // namespace

appbox::test::RawControlPipe::RawControlPipe(const std::string& pipe) : handle_(INVALID_HANDLE_VALUE), last_error_(0)
{
    /*
     * The server creates the next instance of the pipe once a connection was
     * established, so a burst of clients can find every instance busy. The
     * connection is retried until the budget is used up, which is what a client
     * of the launcher does as well.
     */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kConnectTimeoutMs);

    for (;;)
    {
        handle_ = CreateFileA(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            return;
        }

        last_error_ = GetLastError();
        if (last_error_ != ERROR_PIPE_BUSY || std::chrono::steady_clock::now() >= deadline)
        {
            return;
        }

        WaitNamedPipeA(pipe.c_str(), kConnectWaitMs);
    }
}

appbox::test::RawControlPipe::~RawControlPipe()
{
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle_);
    }
}

bool appbox::test::RawControlPipe::IsOpen() const
{
    return handle_ != INVALID_HANDLE_VALUE;
}

std::uint32_t appbox::test::RawControlPipe::LastError() const
{
    return last_error_;
}

bool appbox::test::RawControlPipe::SendPayload(const std::string& payload)
{
    if (!IsOpen())
    {
        return false;
    }

    appbox::RemoteProtocol head;
    head.length = static_cast<std::uint32_t>(payload.size());

    DWORD written = 0;
    if (WriteFile(handle_, &head, sizeof(head), &written, nullptr) == FALSE || written != sizeof(head))
    {
        last_error_ = GetLastError();
        return false;
    }

    if (payload.empty())
    {
        return true;
    }

    written = 0;
    if (WriteFile(handle_, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) == FALSE ||
        written != payload.size())
    {
        last_error_ = GetLastError();
        return false;
    }

    return true;
}

bool appbox::test::RawControlPipe::ReadPayload(std::string& payload, std::uint32_t timeout_ms)
{
    if (!IsOpen())
    {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    appbox::RemoteProtocol head;
    if (!WaitForBytes(static_cast<HANDLE>(handle_), sizeof(head), deadline) ||
        !ReadExact(static_cast<HANDLE>(handle_), &head, sizeof(head)))
    {
        last_error_ = GetLastError();
        return false;
    }

    payload.assign(head.length, '\0');
    if (head.length == 0)
    {
        return true;
    }

    if (!WaitForBytes(static_cast<HANDLE>(handle_), head.length, deadline) ||
        !ReadExact(static_cast<HANDLE>(handle_), payload.data(), head.length))
    {
        last_error_ = GetLastError();
        return false;
    }

    return true;
}

bool appbox::test::RawControlPipe::SendRequest(std::uint64_t id, const std::string& method, const nlohmann::json& param)
{
    nlohmann::json request;
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = method;
    if (!param.is_null())
    {
        request["params"] = param;
    }

    return SendPayload(request.dump());
}

bool appbox::test::RawControlPipe::ReadResponse(nlohmann::json& response, std::uint32_t timeout_ms)
{
    std::string payload;
    if (!ReadPayload(payload, timeout_ms))
    {
        return false;
    }

    response = nlohmann::json::parse(payload, nullptr, false);
    return !response.is_discarded() && response.is_object();
}

bool appbox::test::RawControlPipe::Call(std::uint64_t id, const std::string& method, const nlohmann::json& param,
                                        nlohmann::json& response, std::uint32_t timeout_ms)
{
    if (!SendRequest(id, method, param))
    {
        return false;
    }

    return ReadResponse(response, timeout_ms);
}

int appbox::test::RpcErrorCode(const nlohmann::json& response)
{
    const auto it_error = response.find("error");
    if (it_error == response.end() || !it_error->is_object())
    {
        return 0;
    }

    const auto it_code = it_error->find("code");
    if (it_code == it_error->end() || !it_code->is_number())
    {
        return -1;
    }

    return it_code->get<int>();
}
