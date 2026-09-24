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
    while (!dos_path_w.empty() && dos_path_w.back() == L'\\')
    {
        dos_path_w.pop_back();
    }

    if (dos_path_w.empty())
    {
        SPDLOG_ERROR("overlay filesystem path is empty");
        return false;
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

/**
 * @brief Derive the registry files of the sandbox from the overlay filesystem.
 *
 * Both files live in the registry subdirectory of the overlay, next to the
 * filesystem subdirectory which carries the filesystem overlay. The packer
 * writes them into the archive, so a packaged application carries the virtual
 * registry of the workspace; the sandbox mounts the hive as a private
 * application hive, so a missing file is created on the first mount.
 *
 * @param[in] fs The overlay filesystem root.
 * @param[out] hive_path The DOS path of the hive file.
 * @param[out] isolation_path The DOS path of the isolation file.
 * @return true on success.
 */
static bool MapRegistryFiles(const std::string& fs, std::string& hive_path, std::string& isolation_path)
{
    auto dos_path_w = appbox::UTF8ToWide(fs);
    /* Remove trailing slash */
    while (!dos_path_w.empty() && dos_path_w.back() == L'\\')
    {
        dos_path_w.pop_back();
    }

    if (dos_path_w.empty())
    {
        SPDLOG_ERROR("overlay filesystem path is empty");
        return false;
    }

    std::filesystem::path dir = std::filesystem::path(dos_path_w) / "registry";

    std::error_code ec;
    if (!std::filesystem::create_directories(dir, ec) && ec)
    {
        SPDLOG_ERROR("failed to create the registry directory: {}", ec.message());
        return false;
    }

    hive_path = appbox::WideToUTF8((dir / "user.hiv").wstring());
    isolation_path = appbox::WideToUTF8((dir / "isolation.json").wstring());
    return true;
}

/**
 * @brief Derive the filesystem isolation file of the sandbox from the overlay.
 *
 * The file lives in the overlay root, next to the `filesystem` and the
 * `registry` subdirectories, because the loader treats every child of
 * `filesystem` as a layer of the view. The packer writes the file into the
 * archive, so a packaged application carries the isolation modes of the
 * workspace; the file is not created here, because a missing file means that
 * every entry keeps the default mode of its kind.
 *
 * @param[in] fs The overlay filesystem root.
 * @param[out] isolation_path The DOS path of the isolation file.
 * @return true on success.
 */
static bool MapFilesystemIsolationFile(const std::string& fs, std::string& isolation_path)
{
    auto dos_path_w = appbox::UTF8ToWide(fs);
    /* Remove trailing slash */
    while (!dos_path_w.empty() && dos_path_w.back() == L'\\')
    {
        dos_path_w.pop_back();
    }

    if (dos_path_w.empty())
    {
        SPDLOG_ERROR("overlay filesystem path is empty");
        return false;
    }

    std::filesystem::path file = std::filesystem::path(dos_path_w) / "filesystem-isolation.json";
    isolation_path = appbox::WideToUTF8(file.wstring());
    return true;
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
    MapRegistryFiles(wxGetApp().loader_config.overlay_fs, inject_data.registry_hive_dos_path,
                     inject_data.registry_isolation_dos_path);
    MapFilesystemIsolationFile(wxGetApp().loader_config.overlay_fs, inject_data.filesystem_isolation_dos_path);

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
