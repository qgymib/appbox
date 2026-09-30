#include <wx/wx.h>
#include <spdlog/spdlog.h>
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
#include "utils/HiveMerge.hpp"
#include "utils/ConvertDosPathToNtPath.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/MapBaseFS.hpp"
#include "utils/PatchLayer.hpp"
#include "WString.hpp"
#include "Launcher.hpp"

wxDEFINE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

/**
 * @brief Create the writable upper layer of the sandbox.
 *
 * The upper layer is the `filesystem` subdirectory of the state directory,
 * which the launcher creates here: the state directory never travels in the
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
 * @brief Mount the filesystem layers of one resource root into the view.
 *
 * The layers are appended to the layers of the run, so the root which is
 * mounted first holds the layers the resolution prefers. A root which holds no
 * usable layer is logged and skipped: the resources of the archive are the
 * base image of the run, so a patch package which cannot be used must not fail
 * the run.
 *
 * @param[in] layer_root Root which holds one directory per layer key.
 * @param[in,out] mapped_fs Layers of the view, in mapping order.
 * @return true when the layers of the root were mounted.
 */
static bool MountLowerFS(const std::wstring& layer_root, std::vector<appbox::SandboxLowerFS>& mapped_fs)
{
    const auto ret = appbox::MapBaseFS(appbox::WideToUTF8(layer_root), mapped_fs);
    if (ret != 0)
    {
        SPDLOG_WARN("the filesystem layers of '{}' are skipped: {}", appbox::WideToUTF8(layer_root), ret);
        return false;
    }

    return true;
}

/**
 * @brief Seed the hive the sandbox mounts from the packed registry.
 *
 * The hive below `app` is a read-only resource, while mounting a hive writes
 * to the file: the copy-up of host keys, the whiteout store and the
 * transaction log files all land in the mounted file. The launcher therefore
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

/**
 * @brief Apply the registry of the patch packages to the hive of the sandbox.
 *
 * The hive the sandbox mounts is the base image of the virtual registry: it
 * was seeded from the hive of the archive and it carries the modifications of
 * the earlier runs. The hive of every package which carries one is merged on
 * top of it in the order the packages take effect in, so the keys and the
 * values of the last package which names an entry are the ones the sandboxed
 * process observes, while an entry no package names keeps the content below it.
 *
 * A package whose hive cannot be merged is logged and skipped, exactly like a
 * package which cannot be extracted is: a broken package must not fail the
 * run, and the entries of the layers below it stay in place.
 *
 * @param[in] state_hive Hive the sandbox mounts.
 * @param[in] layers The accepted packages in ascending name order.
 */
static void ApplyPatchHives(const std::wstring& state_hive, const std::vector<appbox::PatchLayer>& layers)
{
    for (const auto& layer : layers)
    {
        if (layer.registry_hive.empty())
        {
            continue;
        }

        std::string error;
        if (!appbox::MergeHiveInto(state_hive, layer.registry_hive, error))
        {
            SPDLOG_ERROR("the registry of the patch package '{}' is skipped: {}", appbox::WideToUTF8(layer.name),
                         error);
            continue;
        }

        SPDLOG_DEBUG("applied the registry of the patch package '{}'", appbox::WideToUTF8(layer.name));
    }
}

AppBoxLauncherRuntime::AppBoxLauncherRuntime(const std::string& log_level)
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = appbox::RandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    const auto& paths = wxGetApp().sandbox_paths;

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);

    /*
     * The resources below `app` are read-only: the layer tree of the packaged
     * application is mounted as the lower filesystem, the injection modules are
     * injected from there and the isolation files of the three domains are
     * handed to the sandbox as they are. The state of the sandbox lives in
     * `data`, which is created here and carries the writable upper layer and
     * the hive the sandbox mounts.
     *
     * The patch packages of the user are the layers above the resources of the
     * archive: the resolution of the view prefers the layer which was mounted
     * first, so the packages are mounted before `app` and the last package of
     * the ascending name order is mounted first. A package which carries no
     * filesystem resource is not mounted at all, which keeps the layers below
     * it in place for that resource.
     *
     * The virtual registry of a package is merged into the hive the sandbox
     * mounts instead of being mounted as a layer of its own, because the
     * sandbox mounts exactly one hive: the keys and the values of the packages
     * are applied to that hive in ascending order, so the last package which
     * names an entry wins while an entry no package names keeps the content of
     * the archive and of the earlier runs.
     */
    const auto patch_layers = appbox::LoadPatchLayers(paths);
    for (auto layer = patch_layers.rbegin(); layer != patch_layers.rend(); ++layer)
    {
        if (!layer->layer_root.empty())
        {
            MountLowerFS(layer->layer_root, inject_data.fs_lower);
        }
    }

    MountLowerFS(paths.LayerRoot(), inject_data.fs_lower);
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

    /*
     * The registry of the packages is applied to the hive the sandbox mounts,
     * so a package overrides the entries of the archive and of the earlier
     * packages while an entry no package names keeps the content below it.
     */
    ApplyPatchHives(paths.StateRegistryHiveFile(), patch_layers);

    inject_data.registry_hive_dos_path = appbox::WideToUTF8(paths.StateRegistryHiveFile());

    /*
     * The isolation modes of the run are the modes of its layers: the file of
     * the resources of the archive comes first and the file of every patch
     * package follows in the order the packages take effect in, so the mode of
     * the last layer which names an entry is the mode the sandboxed process
     * observes. The four domains follow the same rule, and the network
     * configuration and the environment variables are composed the same way:
     * the file of a package which carries no resource of a domain is not
     * listed, so that domain keeps the configuration of the layers below it.
     */
    inject_data.filesystem_isolation_dos_paths.push_back(appbox::WideToUTF8(paths.FilesystemIsolationFile()));
    inject_data.registry_isolation_dos_paths.push_back(appbox::WideToUTF8(paths.RegistryIsolationFile()));
    inject_data.network_isolation_dos_paths.push_back(appbox::WideToUTF8(paths.NetworkIsolationFile()));
    inject_data.environment_isolation_dos_paths.push_back(appbox::WideToUTF8(paths.EnvironmentIsolationFile()));
    for (const auto& layer : patch_layers)
    {
        if (!layer.isolation_file.empty())
        {
            inject_data.filesystem_isolation_dos_paths.push_back(appbox::WideToUTF8(layer.isolation_file));
        }
        if (!layer.registry_isolation_file.empty())
        {
            inject_data.registry_isolation_dos_paths.push_back(appbox::WideToUTF8(layer.registry_isolation_file));
        }
        if (!layer.network_isolation_file.empty())
        {
            inject_data.network_isolation_dos_paths.push_back(appbox::WideToUTF8(layer.network_isolation_file));
        }
        if (!layer.environment_isolation_file.empty())
        {
            inject_data.environment_isolation_dos_paths.push_back(appbox::WideToUTF8(layer.environment_isolation_file));
        }
    }

    inject_data.environment_state_dos_path = appbox::WideToUTF8(paths.StateEnvironmentFile());

    /*
     * Every process the sandbox is injected into writes a log file of its own
     * into the directory of the configuration of the run, which is the
     * directory the launcher itself writes its log into: the logs of a run stay
     * together and a process which crashes leaves its own log behind, because
     * no other process writes that file.
     */
    inject_data.log_dir = appbox::WideToUTF8(paths.root);
    inject_data.log_level = log_level;

    /*
     * The environment state file is written by the RPC method of the launcher
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

    /*
     * The injection modules are resources of the archive and the sandbox is
     * injected from there, so a run writes no module of its own: the state
     * directory keeps carrying what the sandbox really modifies.
     */
    this->inject_data.sandbox32_dos_path = appbox::WideToUTF8(paths.Sandbox32Dll());
    this->inject_data.sandbox64_dos_path = appbox::WideToUTF8(paths.Sandbox64Dll());

    this->pipe_server = appbox::RemoteServer::Create(this->inject_data.pipe_path);
    SPDLOG_DEBUG("pipe listen on {}", this->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit(this->pipe_server);
    this->pipe_server->Start();
}

AppBoxLauncherRuntime::~AppBoxLauncherRuntime()
{
    this->pipe_server.reset();
}
