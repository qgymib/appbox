/*
 * <winsock2.h> has to precede <windows.h>, which the headers below pull in
 * (<Shlobj.h>, the project headers): it defines _WINSOCKAPI_, so <windows.h>
 * skips the winsock 1.1 header, which cannot be included next to the winsock 2
 * header the RPC server reaches through asio.
 */
#include <winsock2.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <Shlobj.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "WinAPI.h"
#include "KnownFolder.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/HiveMerge.hpp"
#include "utils/ConvertDosPathToNtPath.hpp"
#include "utils/MapBaseFS.hpp"
#include "utils/PatchLayer.hpp"
#include "WString.hpp"
#include "Launcher.hpp"
#include "Splash.hpp"

AppBoxLauncher& LauncherApp()
{
    static AppBoxLauncher instance;
    return instance;
}

void ReportStartupStage(appbox::StartupStage stage, double fraction)
{
    if (LauncherApp().splash != nullptr)
    {
        LauncherApp().splash->SetStage(stage, fraction);
    }
}

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
 * @brief Whether a text starts with a prefix, ignoring the case.
 * @param[in] text Text to inspect.
 * @param[in] prefix Prefix to compare against.
 * @return true when \p text starts with \p prefix.
 */
static bool StartsWithIgnoreCase(const std::wstring& text, const std::wstring& prefix)
{
    if (text.size() < prefix.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index)
    {
        wchar_t left = text[index];
        wchar_t right = prefix[index];
        if (left >= L'A' && left <= L'Z')
        {
            left = static_cast<wchar_t>(left - L'A' + L'a');
        }
        if (right >= L'A' && right <= L'Z')
        {
            right = static_cast<wchar_t>(right - L'A' + L'a');
        }
        if (left != right)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Remove a hive file together with the files the mount keeps next to it.
 *
 * Mounting a hive creates the transaction log of the hive beside it, so
 * removing the hive alone would leave a log which belongs to a file that no
 * longer exists. The log files are named after the hive, so every sibling
 * which starts with its name is removed as well.
 *
 * @param[in] hive DOS path of the hive.
 */
static void RemoveHiveFiles(const std::wstring& hive)
{
    const std::filesystem::path path(hive);
    const std::wstring          prefix = path.filename().wstring() + L".";

    std::error_code ec;
    std::filesystem::remove(path, ec);

    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path(), ec))
    {
        const auto name = entry.path().filename().wstring();
        if (name.size() > prefix.size() && StartsWithIgnoreCase(name, prefix))
        {
            std::error_code remove_ec;
            std::filesystem::remove(entry.path(), remove_ec);
        }
    }
}

/**
 * @brief Read the registry isolation file of a layer.
 *
 * @param[in] path DOS path of the isolation file.
 * @param[out] document The document to fill.
 * @param[out] error Error description on failure.
 * @return true when the file was read.
 */
static bool ReadRegistryIsolationDocument(const std::wstring& path, appbox::registry_isolation::Document& document,
                                          std::string& error)
{
    error.clear();

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        error = "the registry isolation file does not exist";
        return false;
    }

    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    try
    {
        document = nlohmann::json::parse(text).get<appbox::registry_isolation::Document>();
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }

    if (document.version != appbox::registry_isolation::kVersion)
    {
        error = "unsupported isolation file version";
        return false;
    }

    return true;
}

/**
 * @brief Rebuild the read-only image of the virtual registry.
 *
 * The image carries the layers of the run which the sandboxed process never
 * modifies: the registry of `app` and the hive of every patch package, applied
 * in the order the packages take effect in. It is rebuilt at every start, so a
 * rebuilt archive and a replaced package reach the next run, while the state
 * hive of the sandbox stays the highest layer of the view and keeps every
 * modification the sandboxed process made.
 *
 * The isolation modes of a package are applied to the image right after its
 * content, because a package may name a mode which differs from the mode the
 * layer below it packed the content with: a package which hides an entry of a
 * lower layer hides its content, and a package which gives a hidden entry
 * another mode makes the content of the lower layer visible again (see
 * `appbox::ApplyHiveIsolationModes`).
 *
 * A package whose hive cannot be merged is logged and skipped, exactly like a
 * package which cannot be extracted is: a broken package must not fail the
 * run, and the entries of the layers below it stay in place. A package whose
 * isolation file cannot be read is logged and skipped as well.
 *
 * The state hive of a layout which did not carry an image yet holds the content
 * of the archive and of the packages merged with the modifications of the runs.
 * It is dropped once, because keeping it would shadow the image and hide the
 * content of a rebuilt archive.
 *
 * @param[in] paths The resolved layout of the run.
 * @param[in] layers The accepted packages in ascending name order.
 * @return Error description, empty on success. A run whose image cannot be
 *         rebuilt keeps the image of the earlier start when it is still there.
 */
static std::string BuildRegistryImage(const appbox::SandboxPaths& paths, const std::vector<appbox::PatchLayer>& layers)
{
    const std::filesystem::path image_dir(paths.StateRegistryImageDir());
    const std::filesystem::path image_hive(paths.StateRegistryImageHiveFile());

    std::error_code ec;

    const std::filesystem::path state_hive(paths.StateRegistryHiveFile());
    if (!std::filesystem::exists(image_dir, ec) && std::filesystem::exists(state_hive, ec))
    {
        SPDLOG_INFO("the state hive of an earlier layout is dropped: {}", appbox::WideToUTF8(state_hive.wstring()));
        RemoveHiveFiles(state_hive.wstring());
    }

    /* The image and the log files of its mount are replaced at every start. */
    std::filesystem::remove_all(image_dir, ec);
    ec.clear();
    std::filesystem::create_directories(image_dir, ec);
    if (ec)
    {
        return fmt::format("failed to create '{}': {}", appbox::WideToUTF8(image_dir.wstring()), ec.message());
    }

    const std::filesystem::path packed_hive(paths.RegistryHiveFile());
    if (std::filesystem::is_regular_file(packed_hive, ec))
    {
        std::filesystem::copy_file(packed_hive, image_hive, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            return fmt::format("failed to copy '{}' into '{}': {}", appbox::WideToUTF8(packed_hive.wstring()),
                               appbox::WideToUTF8(image_hive.wstring()), ec.message());
        }
    }
    else
    {
        SPDLOG_INFO("the archive carries no registry hive, the image starts empty");
    }

    std::string error;
    if (!appbox::EnsureHiveFile(image_hive.wstring(), error))
    {
        return error;
    }

    for (const auto& layer : layers)
    {
        if (!layer.registry_hive.empty())
        {
            if (!appbox::MergeHiveInto(image_hive.wstring(), layer.registry_hive, error))
            {
                SPDLOG_ERROR("the registry of the patch package '{}' is skipped: {}", appbox::WideToUTF8(layer.name),
                             error);
            }
            else
            {
                SPDLOG_DEBUG("applied the registry of the patch package '{}' to the image",
                             appbox::WideToUTF8(layer.name));
            }
        }

        if (layer.registry_isolation_file.empty())
        {
            continue;
        }

        appbox::registry_isolation::Document document;
        if (!ReadRegistryIsolationDocument(layer.registry_isolation_file, document, error))
        {
            SPDLOG_ERROR("the registry isolation of the patch package '{}' is skipped: {}",
                         appbox::WideToUTF8(layer.name), error);
            continue;
        }

        if (!appbox::ApplyHiveIsolationModes(image_hive.wstring(), document.keys, document.values, error))
        {
            SPDLOG_ERROR("the registry isolation of the patch package '{}' is skipped: {}",
                         appbox::WideToUTF8(layer.name), error);
            continue;
        }

        SPDLOG_DEBUG("applied the registry isolation of the patch package '{}' to the image",
                     appbox::WideToUTF8(layer.name));
    }

    return {};
}

AppBoxLauncherRuntime::AppBoxLauncherRuntime(const std::string& log_level, const std::wstring& log_dir,
                                             const std::string& pipe_path)
{
    const auto& paths = LauncherApp().sandbox_paths;

    /*
     * The control pipe is the RPC pipe of the sandbox, and its name is derived
     * from the identifier of the sandbox alone: every launcher of one sandbox
     * competes for the same name, while two sandboxes never share one. The name
     * is resolved by the caller, which won the race for the pipe before the
     * runtime of the run was created.
     */
    this->inject_data.pipe_path = pipe_path;

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
     * The registry of a run is a view of the state hive of the sandbox and of a
     * read-only image which carries the content of the archive and of the
     * packages: the state of the sandbox wins over the image, which wins over
     * the real registry, so a modification of the sandboxed process is never
     * reset by a layer below it. The image is rebuilt at every start, so the
     * content of a rebuilt archive and of a replaced package reaches the next
     * run all the same.
     */
    ReportStartupStage(appbox::StartupStage::LoadingPatches, 0.0);

    const auto patch_layers = appbox::LoadPatchLayers(paths, [](std::size_t done, std::size_t total, double fraction) {
        /* The packages of the directory are the whole of this stage. */
        const double packages = total > 0 ? static_cast<double>(total) : 1.0;
        ReportStartupStage(appbox::StartupStage::LoadingPatches, (static_cast<double>(done) + fraction) / packages);
    });

    ReportStartupStage(appbox::StartupStage::MountingSandbox, 0.0);

    for (auto layer = patch_layers.rbegin(); layer != patch_layers.rend(); ++layer)
    {
        if (!layer->layer_root.empty())
        {
            MountLowerFS(layer->layer_root, inject_data.fs_lower);
        }
    }

    MountLowerFS(paths.LayerRoot(), inject_data.fs_lower);
    MapOverlayFS(paths.state, inject_data.fs_upper);

    ReportStartupStage(appbox::StartupStage::MountingSandbox, 0.3);

    /*
     * The variables the sandbox expands in the values of the workspace. The
     * name of each one is the layer key of a known folder without its `#`
     * delimiters, so the supported list follows the table of the known folders
     * and a folder which is added later brings its variable with it.
     *
     * Every variable carries the folder of a 64 bit process and the folder of a
     * 32 bit process, because the operating system redirects some of them. The
     * sandbox picks the view which matches the process it is injected into, so
     * a 32 bit application which starts a 64 bit program gives the child the
     * paths of a 64 bit process.
     */
    for (const auto& variable : appbox::KnownFolderVariables())
    {
        inject_data.variables.push_back(appbox::SandboxVariable{ variable.name, variable.path, variable.path_x86 });
    }

    const std::string image_error = BuildRegistryImage(paths, patch_layers);
    if (!image_error.empty())
    {
        SPDLOG_ERROR("{}", image_error);
    }

    inject_data.registry_hive_dos_path = appbox::WideToUTF8(paths.StateRegistryHiveFile());
    {
        std::error_code ec;
        const auto      image_hive = paths.StateRegistryImageHiveFile();
        if (std::filesystem::is_regular_file(image_hive, ec))
        {
            inject_data.registry_image_hive_dos_path = appbox::WideToUTF8(image_hive);
        }
        else
        {
            /*
             * A run without an image still isolates the registry, but the
             * sandboxed process then only sees the state of the sandbox and the
             * real registry: the content of the archive is missing.
             */
            SPDLOG_WARN("the run has no registry image: the content of the archive is not visible");
        }
    }

    ReportStartupStage(appbox::StartupStage::MountingSandbox, 0.6);

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
     * into the log directory of the run, which is the directory the launcher
     * itself writes its log into: the logs of a run stay together and a process
     * which crashes leaves its own log behind, because no other process writes
     * that file. A run which asks for no log file hands the sandbox no
     * directory at all, so no process of that run creates a log file.
     */
    inject_data.log_dir = appbox::WideToUTF8(log_dir);
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
     * The injection modules are resources of the launcher which the run
     * released into the cache directory before the runtime was created, so
     * the sandbox is injected from the files of that run.
     */
    this->inject_data.sandbox32_dos_path = appbox::WideToUTF8(LauncherApp().sandbox_modules.sandbox32_dll);
    this->inject_data.sandbox64_dos_path = appbox::WideToUTF8(LauncherApp().sandbox_modules.sandbox64_dll);

    ReportStartupStage(appbox::StartupStage::MountingSandbox, 1.0);
}

AppBoxLauncherRuntime::~AppBoxLauncherRuntime() = default;
