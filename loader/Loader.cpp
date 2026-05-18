#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <cmrc/cmrc.hpp>
#include <sstream>
#include <chrono>
#include "Random.hpp"
#include "rpc/__init__.hpp"
#include "Loader.hpp"

CMRC_DECLARE(sandbox_resource);
wxDEFINE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

AppBoxLoaderRuntime::AppBoxLoaderRuntime()
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = appbox::RandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);
    this->temp_dir = std::filesystem::temp_directory_path() / unique_path;
    SPDLOG_INFO("temp_dir: {}", this->temp_dir.string());

    std::filesystem::create_directory(this->temp_dir);

    this->inject_data.sandbox32_path = (this->temp_dir / "sandbox32.dll").string();
    this->inject_data.sandbox64_path = (this->temp_dir / "sandbox64.dll").string();

    auto fs = cmrc::sandbox_resource::get_filesystem();
    {
        auto dll = fs.open("lib/AppBoxSandbox32.dll");

        std::ofstream ofs(this->inject_data.sandbox32_path, std::ios::binary);
        ofs.write(dll.begin(), dll.size());
    }
    {
        auto          dll = fs.open("lib/AppBoxSandbox64.dll");
        std::ofstream ofs(this->inject_data.sandbox64_path, std::ios::binary);
        ofs.write(dll.begin(), dll.size());
    }

#if 0
    this->pipe_server = RemoteServer::Create(this->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit();
    loader->pipe_server->Start();
#endif
}

AppBoxLoaderRuntime::~AppBoxLoaderRuntime()
{
    this->pipe_server.reset();
    std::filesystem::remove_all(this->temp_dir);
}
