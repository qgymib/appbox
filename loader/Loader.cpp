#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <cmrc/cmrc.hpp>
#include <sstream>
#include <chrono>
#include <Shlobj.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include "sandbox/utils/WinAPI.h"
#include "Random.hpp"
#include "rpc/__init__.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/ConvertDosPathToNtPath.hpp"
#include "utils/KnownFolder.hpp"
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

/**
 * @brief Create the writable upper layer of the sandbox.
 *
 * The upper layer is the `filesystem` subdirectory of the state directory,
 * which the loader creates here: the state directory never travels in the
 * archive, so deleting it resets the sandbox to the packed application.
 *
 * @param[in] state_dir The state directory of the sandbox.
 * @param[out] mapped_fs The NT path of the created upper layer.
 * @return true when the layer exists afterwards.
 */
static bool MapOverlayFS(const std::wstring& state_dir, std::string& mapped_fs)
{
    auto dos_path_w = state_dir;
    /* Remove trailing slash */
    while (!dos_path_w.empty() && dos_path_w.back() == L'\\')
    {
        dos_path_w.pop_back();
    }

    if (dos_path_w.empty())
    {
        SPDLOG_ERROR("the state directory of the sandbox is empty");
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
 * @brief Seed the hive the sandbox mounts from the packed registry.
 *
 * The hive below `app` is a read-only resource, while mounting a hive writes
 * to the file: the copy-up of host keys, the whiteout store and the
 * transaction log files all land in the mounted file. The loader therefore
 * copies the packed hive into the state directory of the sandbox on the first
 * run and the sandbox mounts that copy. A hive which already exists is kept,
 * so the modifications of an earlier run survive and deleting the state
 * directory resets the sandbox to the registry of the archive.
 *
 * A missing packed hive is not an error: the sandbox creates an empty hive
 * when it mounts a file which does not exist yet.
 *
 * @param[in] packed_hive Hive inside the resources of the application.
 * @param[in] state_hive Hive the sandbox mounts.
 * @return Error description, empty on success.
 */
static std::string SeedRegistryHive(const std::wstring& packed_hive, const std::wstring& state_hive)
{
    const std::filesystem::path target(state_hive);

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec)
    {
        return fmt::format("failed to create '{}': {}", appbox::WideToUTF8(target.parent_path().wstring()),
                           ec.message());
    }

    if (std::filesystem::exists(target, ec))
    {
        /* The hive of an earlier run belongs to the sandbox. */
        return {};
    }

    if (!std::filesystem::exists(packed_hive, ec))
    {
        SPDLOG_INFO("the archive carries no registry hive, the sandbox creates an empty one");
        return {};
    }

    std::filesystem::copy_file(packed_hive, target, std::filesystem::copy_options::none, ec);
    if (ec)
    {
        return fmt::format("failed to seed '{}': {}", appbox::WideToUTF8(state_hive), ec.message());
    }

    SPDLOG_DEBUG("seeded the registry hive from '{}'", appbox::WideToUTF8(packed_hive));
    return {};
}

AppBoxLoaderRuntime::AppBoxLoaderRuntime()
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = appbox::RandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    const auto& paths = wxGetApp().sandbox_paths;

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);

    /*
     * The resources below `app` are read-only: the layer tree of the packaged
     * application is mounted as the lower filesystem and the isolation files of
     * the three domains are handed to the sandbox as they are. The state of the
     * sandbox lives in `data`, which is created here and carries the writable
     * upper layer, the hive the sandbox mounts and the injected DLLs.
     */
    MapBaseFS(appbox::WideToUTF8(paths.LayerRoot()), inject_data.fs_lower);
    MapOverlayFS(paths.state, inject_data.fs_upper);

    /*
     * The variables the sandbox expands in the values of the workspace. The
     * name of each one is the layer key of a preset directory without its `#`
     * delimiters, so the supported list follows the preset directories of the
     * packer and a preset directory which is added later brings its variable
     * with it.
     */
    for (const auto& variable : appbox::KnownFolderVariables())
    {
        inject_data.variables.push_back(appbox::SandboxVariable{ variable.name, variable.path });
    }

    {
        const auto error = SeedRegistryHive(paths.RegistryHiveFile(), paths.StateRegistryHiveFile());
        if (!error.empty())
        {
            SPDLOG_ERROR("{}", error);
        }
    }

    inject_data.registry_hive_dos_path = appbox::WideToUTF8(paths.StateRegistryHiveFile());
    inject_data.registry_isolation_dos_path = appbox::WideToUTF8(paths.RegistryIsolationFile());
    inject_data.filesystem_isolation_dos_path = appbox::WideToUTF8(paths.FilesystemIsolationFile());
    inject_data.network_isolation_dos_path = appbox::WideToUTF8(paths.NetworkIsolationFile());
    inject_data.environment_isolation_dos_path = appbox::WideToUTF8(paths.EnvironmentIsolationFile());
    inject_data.environment_state_dos_path = appbox::WideToUTF8(paths.StateEnvironmentFile());

    /*
     * The environment state file is written by the RPC method of the loader
     * while the sandboxed application changes its environment, so its directory
     * exists before the application runs.
     */
    {
        const std::filesystem::path state_file(paths.StateEnvironmentFile());
        std::error_code             ec;
        std::filesystem::create_directories(state_file.parent_path(), ec);
        if (ec)
        {
            SPDLOG_ERROR("failed to create '{}': {}", appbox::WideToUTF8(state_file.parent_path().wstring()),
                         ec.message());
        }
    }

    {
        const std::filesystem::path state(paths.state);
        this->inject_data.sandbox32_dos_path = appbox::WideToUTF8((state / L"sandbox32.dll").wstring());
        this->inject_data.sandbox64_dos_path = appbox::WideToUTF8((state / L"sandbox64.dll").wstring());
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
