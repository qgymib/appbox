#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>
#include "rpc/__init__.hpp"
#include "CRC32.hpp"
#include "Loader.hpp"

appbox::Loader* appbox::loader = nullptr;

static std::string GenerateInjectDataPipePath()
{
    std::stringstream ss_crc32;
    ss_crc32 << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
             << appbox::CRC32::Update(0, appbox::loader->executable_path.c_str(),
                                      appbox::loader->executable_path.size());

    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    return fmt::format(R"(\\.\pipe\appbox-{}-{})", timestamp, ss_crc32.str());
}

void appbox::InitLoader()
{
    if (loader != nullptr)
    {
        return;
    }
    loader = new Loader();

    /* Start pipe server */
    loader->inject_data.pipe_path = GenerateInjectDataPipePath();
    loader->pipe_server = RemoteServer::Create(loader->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit();
    loader->pipe_server->Start();
}

void appbox::ExitLoader()
{
    delete loader;
    loader = nullptr;
}
