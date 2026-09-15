#include <gtest/gtest.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include "RemoteClient.hpp"
#include "RemoteServer.hpp"

namespace
{

/**
 * @brief Number of handles owned by the test process.
 * @return The handle count, zero when it cannot be queried.
 */
DWORD CountHandles()
{
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count))
    {
        return 0;
    }

    return count;
}

/**
 * @brief Build a unique temporary file path.
 * @return The path of a file which does not exist yet.
 */
std::string MakeTempFilePath()
{
    char        temp_dir[MAX_PATH] = {};
    const DWORD length = GetTempPathA(MAX_PATH, temp_dir);
    EXPECT_GT(length, 0u);

    std::random_device rd;
    std::mt19937_64    gen(rd());

    return std::string(temp_dir) + "appbox_unit_" + std::to_string(gen()) + ".tmp";
}

/**
 * @brief Build a unique named pipe path.
 * @return The pipe path.
 */
std::string MakePipePath()
{
    std::random_device rd;
    std::mt19937_64    gen(rd());

    return R"(\\.\pipe\appbox_unit_)" + std::to_string(gen());
}

/**
 * @brief Create a regular file which is not a named pipe.
 * @param[in] path Path of the file.
 */
void CreateRegularFile(const std::string& path)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << "this is not a named pipe";
}

} // namespace

/**
 * @brief Opening the client end of a path which is not a named pipe fails, and
 *        the handle opened for it is closed again.
 */
TEST(UnitRemoteClient, StartOnRegularFileFailsWithoutLeakingHandle)
{
    const std::string file_path = MakeTempFilePath();
    CreateRegularFile(file_path);

    const DWORD before = CountHandles();

    {
        auto client = appbox::RemoteClient::Create(file_path);
        EXPECT_FALSE(client->Start());
    }

    const DWORD after = CountHandles();
    EXPECT_LE(after, before + 1); /* Allow a single handle of noise. */

    std::filesystem::remove(file_path);
}

/**
 * @brief Repeated failing connections do not accumulate handles.
 */
TEST(UnitRemoteClient, RepeatedFailedStartDoesNotLeakHandles)
{
    const std::string file_path = MakeTempFilePath();
    CreateRegularFile(file_path);

    /* Warm up, the first attempt may create lazily initialized resources. */
    {
        auto client = appbox::RemoteClient::Create(file_path);
        EXPECT_FALSE(client->Start());
    }

    const DWORD before = CountHandles();

    for (int i = 0; i < 10; ++i)
    {
        auto client = appbox::RemoteClient::Create(file_path);
        EXPECT_FALSE(client->Start());
    }

    const DWORD after = CountHandles();
    EXPECT_LE(after, before + 1);

    std::filesystem::remove(file_path);
}

/**
 * @brief A pipe which does not exist makes the start fail.
 */
TEST(UnitRemoteClient, StartOnMissingPipeFails)
{
    auto client = appbox::RemoteClient::Create(MakePipePath());
    EXPECT_FALSE(client->Start());
}

/**
 * @brief An error response of the server is delivered to the caller instead of
 *        being dropped or throwing.
 */
TEST(UnitRemoteClient, CallUnknownMethodReturnsError)
{
    const std::string pipe_path = MakePipePath();

    auto server = appbox::RemoteServer::Create(pipe_path);
    server->Start();

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start());

    auto rsp = client->Call("appbox_unit_missing_method", nlohmann::json::object()).get();

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, -32601);
}

/**
 * @brief A regular request and response round trip still works.
 */
TEST(UnitRemoteClient, CallRegisteredMethod)
{
    const std::string pipe_path = MakePipePath();

    auto server = appbox::RemoteServer::Create(pipe_path);
    server->RegisterMethod("appbox_unit_echo", [server](uint64_t id, const nlohmann::json& param) {
        server->SendResponse(id, param);
    });
    server->Start();

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start());

    auto rsp = client->Call("appbox_unit_echo", "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");
}
