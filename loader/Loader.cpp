#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <cmrc/cmrc.hpp>
#include <sstream>
#include <chrono>
#include <Shlobj.h>
#include <algorithm>
#include "sandbox/utils/WinAPI.h"
#include "Random.hpp"
#include "rpc/__init__.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/ConvertDosPathToNtPath.hpp"
#include "utils/MapBaseFS.hpp"
#include "WString.hpp"
#include "Loader.hpp"

CMRC_DECLARE(sandbox_resource);
wxDEFINE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

static void ExtractSandboxDll(const std::wstring& dll32_path, const std::wstring& dll64_path)
{
    auto fs = cmrc::sandbox_resource::get_filesystem();
    {
        auto dll = fs.open("lib/AppBoxSandbox32.dll");

        std::ofstream ofs(dll32_path, std::ios::binary | std::ios::trunc);
        ofs.write(dll.begin(), dll.size());
    }
    {
        auto          dll = fs.open("lib/AppBoxSandbox64.dll");
        std::ofstream ofs(dll64_path, std::ios::binary | std::ios::trunc);
        ofs.write(dll.begin(), dll.size());
    }
}

static void ExtractSandboxDll(const std::string& dll32_path, const std::string& dll64_path)
{
    auto dll32_path_w = appbox::UTF8ToWide(dll32_path);
    auto dll64_path_w = appbox::UTF8ToWide(dll64_path);
    ExtractSandboxDll(dll32_path_w, dll64_path_w);
}

static bool MapOverlayFS(const std::string& fs, std::string& mapped_fs)
{
    auto dos_path_w = appbox::UTF8ToWide(fs);
    /* Remove trailing slash */
    while (dos_path_w.back() == L'\\')
    {
        dos_path_w.pop_back();
    }
    dos_path_w += L"\\filesystem";

    std::wstring nt_path_w;
    if (appbox::ConvertDosPathToNtPath(dos_path_w, nt_path_w))
    {
        return false;
    }

    mapped_fs = appbox::WideToUTF8(nt_path_w);

    /* Create directory */
    return std::filesystem::create_directories(dos_path_w);
}

AppBoxLoaderRuntime::AppBoxLoaderRuntime()
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = appbox::RandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);
    for (const auto& f : wxGetApp().loader_config.base_fs)
    {
        appbox::MapBaseFS(f, inject_data.fs_lower);
    }
    MapOverlayFS(wxGetApp().loader_config.overlay_fs, inject_data.fs_upper);

    {
        auto                  w_overlay_path = appbox::UTF8ToWide(wxGetApp().loader_config.overlay_fs);
        std::filesystem::path overlay_path(w_overlay_path);
        this->inject_data.sandbox32_dos_path = appbox::WideToUTF8((overlay_path / "sandbox32.dll").wstring());
        this->inject_data.sandbox64_dos_path = appbox::WideToUTF8((overlay_path / "sandbox64.dll").wstring());
    }

    ExtractSandboxDll(inject_data.sandbox32_dos_path, inject_data.sandbox64_dos_path);

    this->pipe_server = appbox::RemoteServer::Create(this->inject_data.pipe_path);
    SPDLOG_DEBUG("pipe listen on {}", this->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit(this->pipe_server);
    this->pipe_server->Start();
}

AppBoxLoaderRuntime::~AppBoxLoaderRuntime()
{
    this->pipe_server.reset();
}
