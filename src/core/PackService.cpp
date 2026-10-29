#include "PackService.hpp"
#include "ApplicationIcon.hpp"
#include "EnvironmentIsolationFile.hpp"
#include "FilesystemIsolationFile.hpp"
#include "FilesystemMarker.hpp"
#include "LauncherResources.hpp"
#include "SandboxConfigPayload.hpp"
#include "NetworkIsolationFile.hpp"
#include "PeResourcePatch.hpp"
#include "PresetDirectory.hpp"
#include "RegistryHive.hpp"
#include "RegistryIsolationFile.hpp"
#include "SandboxLayout.hpp"
#include "StartupFileType.hpp"
#include "ZipWriter.hpp"
#include "Config.hpp"
#include "ExecutablePayload.hpp"
#include "WString.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Convert a relative path into a zip entry suffix.
 *
 * Zip entry names use forward slashes; the input uses native separators.
 *
 * @param[in] relative Path relative to the import root.
 * @return The zip entry suffix with forward slashes.
 */
std::string ToZipEntrySuffix(const std::wstring& relative)
{
    auto entry = appbox::WideToUTF8(relative);
    std::replace(entry.begin(), entry.end(), '\\', '/');
    return entry;
}

/**
 * @brief Add a directory entry unless it was already added.
 *
 * Imported folders and imported files share the same layer tree, so the same
 * directory prefix can be reached twice; libzip rejects duplicate entries.
 *
 * @param[in,out] writer The zip writer.
 * @param[in,out] added Entry names which were already added.
 * @param[in] entry Directory entry name.
 * @return Error description, empty on success.
 */
std::string EnsureDirectory(appbox::ZipWriter& writer, std::set<std::string>& added, const std::string& entry)
{
    if (added.count(entry) != 0)
    {
        return {};
    }

    std::string error;
    if (!writer.AddDirectory(entry, error))
    {
        return error;
    }

    added.insert(entry);
    return {};
}

/**
 * @brief Build the path a progress report shows for one packed file.
 *
 * The reported path is relative to the import root and prefixed by the import
 * name, which keeps it readable for deeply nested host folders.
 *
 * @param[in] root Import name or target directory inside the sandbox view.
 * @param[in] relative Path below the import root.
 * @return The display path, e.g. `L"MyApp\bin\tool.exe"`.
 */
std::wstring DisplayPath(const std::wstring& root, const std::wstring& relative)
{
    if (root.empty())
    {
        return relative;
    }
    return root + L"\\" + relative;
}

/**
 * @brief Get the startup file the launcher inherits its file icon from.
 *
 * The icon and the file properties of the launcher are read from a program
 * image, so the first startup file which is a program is used. A session which
 * starts scripts only falls back to its first startup file: the image of a
 * script cannot be read, so the launcher keeps its own icon and its own file
 * properties and the reason is logged.
 *
 * @param[in] files The startup files in startup order.
 * @return The first program of the list, the first file when the list holds no
 *         program, null when the list is empty.
 */
const appbox::StartupFile* IconSourceOf(const std::vector<appbox::StartupFile>& files)
{
    if (files.empty())
    {
        return nullptr;
    }

    for (const auto& file : files)
    {
        if (appbox::StartupFileKindOf(file.relative_path) == appbox::StartupFileKind::Program)
        {
            return &file;
        }
    }

    return &files.front();
}

/**
 * @brief Recursively add one imported folder below a zip prefix.
 *
 * Every visited directory becomes a directory entry, so empty folders are
 * preserved. Regular files are streamed from disk.
 *
 * @param[in,out] writer The zip writer.
 * @param[in] source Host folder to add.
 * @param[in] prefix Zip entry prefix of the import, e.g.
 *                   `"filesystem/#ProgramFiles#/MyApp"`.
 * @param[in] display_root Import name used by the progress reports.
 * @param[in,out] added Directory entries which were already added.
 * @param[in,out] done Number of files already packed.
 * @param[in] total Total number of files to pack.
 * @param[in] progress Progress callback, may be empty.
 * @return Error description, empty on success.
 */
std::string AddImportedFolder(appbox::ZipWriter& writer, const std::wstring& source, const std::string& prefix,
                              const std::wstring& display_root, std::set<std::string>& added, std::size_t& done,
                              std::size_t total, const appbox::BuildProgressCallback& progress)
{
    const auto root = std::filesystem::path(source).lexically_normal();
    const auto root_string = root.wstring();

    std::error_code ec;
    auto            it = std::filesystem::recursive_directory_iterator(root, ec);
    const auto      end = std::filesystem::recursive_directory_iterator();
    if (ec)
    {
        return "failed to enumerate '" + appbox::WideToUTF8(root_string) + "'";
    }

    while (it != end)
    {
        const auto full = it->path().wstring();

        /* The iterator always produces paths below the normalized root. */
        auto relative = full.substr(root_string.size());
        while (!relative.empty() && (relative.front() == L'\\' || relative.front() == L'/'))
        {
            relative.erase(relative.begin());
        }
        const auto entry = prefix + "/" + ToZipEntrySuffix(relative);

        if (it->is_directory(ec))
        {
            if (!ec)
            {
                const auto error = EnsureDirectory(writer, added, entry);
                if (!error.empty())
                {
                    return error;
                }
            }
        }
        else if (it->is_regular_file(ec))
        {
            done++;

            /*
             * The file is reported before it is written: a single large file
             * keeps the dialog busy for a while, so naming it while it is
             * being packed is what makes the report useful.
             */
            if (progress && !progress(appbox::BuildProgress{ appbox::BuildStage::Packing, done, total,
                                                             DisplayPath(display_root, relative) }))
            {
                return appbox::kBuildCancelledError;
            }

            if (!ec)
            {
                std::string error;
                if (!writer.AddFileDisk(full, entry, error))
                {
                    return error;
                }
            }
        }

        it.increment(ec);
        if (ec)
        {
            return "failed to enumerate '" + appbox::WideToUTF8(full) + "'";
        }
    }

    return {};
}

/**
 * @brief Join a resource root with an entry below it.
 *
 * @param[in] root Root of the resource tree: the name of the resource
 *                 directory of a standalone archive, or an empty string for a
 *                 patch package, which roots the resources at the archive root.
 * @param[in] app_relative Entry name relative to the resource root, which is
 *                         one of the `*AppRelative` names of
 *                         `common/SandboxLayout.hpp` or the name of an entry
 *                         which lives directly below that root, like the
 *                         sandbox injection modules.
 * @return The entry name relative to the archive root.
 */
std::string ResourceEntry(const std::string& root, const char* app_relative)
{
    return root.empty() ? std::string(app_relative) : root + "/" + app_relative;
}

/**
 * @brief Write the read-only resources of the model into an archive.
 *
 * The resources are the filesystem layers with their isolation modes, the
 * virtual registry with its isolation modes, the network configuration and the
 * environment variables of the workspace. A standalone archive roots them
 * below `app` while a patch package roots them at the archive root, which is
 * the only difference between the two products; the tree itself, the order of
 * the entries and the progress reports are shared.
 *
 * The `data` directory of the sandbox never travels in either product: the
 * launcher creates it at run time, so deleting it resets the sandbox to the
 * state the archive carries.
 *
 * @param[in,out] writer The zip writer.
 * @param[in] root Root of the resource tree, see ResourceEntry().
 * @param[in] model The pack model.
 * @param[in] registry Virtual registry of the workspace, written as a hive file
 *                     and an isolation file.
 * @param[in] isolation Isolation modes of the virtual filesystem.
 * @param[in] network DNS redirections of the network workspace.
 * @param[in] environment Environment variables of the workspace.
 * @param[in] total Total number of files the progress reports announce.
 * @param[in] progress Progress callback, may be empty.
 * @return Error description, empty on success.
 */
std::string WriteResourceTree(appbox::ZipWriter& writer, const std::string& root, const appbox::PackModel& model,
                              const appbox::RegistryModel& registry, const appbox::FilesystemIsolationModel& isolation,
                              const appbox::NetworkModel& network, const appbox::EnvironmentModel& environment,
                              std::size_t total, const appbox::BuildProgressCallback& progress)
{
    const auto layer_prefix = ResourceEntry(root, appbox::layout::kFilesystemDirName);
    const auto registry_prefix = ResourceEntry(root, appbox::layout::kRegistryDirName);
    const auto network_prefix = ResourceEntry(root, appbox::layout::kNetworkDirName);
    const auto environment_prefix = ResourceEntry(root, appbox::layout::kEnvironmentDirName);

    std::set<std::string> added;
    std::string           error;

    /* A patch package has no resource directory of its own. */
    if (!root.empty())
    {
        error = EnsureDirectory(writer, added, root);
        if (!error.empty())
        {
            return error;
        }
    }

    for (const auto& directory : { layer_prefix, registry_prefix, network_prefix, environment_prefix })
    {
        error = EnsureDirectory(writer, added, directory);
        if (!error.empty())
        {
            return error;
        }
    }

    /*
     * The virtual registry of the workspace travels as a hive file next to its
     * isolation file. The launcher seeds the hive into the state directory of the
     * sandbox on the first run, because mounting a hive writes to the file and
     * the resources stay read-only; the isolation file holds the modes which
     * decide which host entries stay visible.
     */
    std::vector<std::uint8_t> hive;
    if (!appbox::BuildRegistryHiveBytes(registry, hive, error))
    {
        return error;
    }
    if (!writer.AddFileBuffer(ResourceEntry(root, appbox::layout::kRegistryHiveAppRelative), hive.data(), hive.size(),
                              error))
    {
        return error;
    }

    std::string registry_isolation;
    if (!appbox::BuildRegistryIsolationFile(registry, registry_isolation, error))
    {
        return error;
    }
    if (!writer.AddFileBuffer(ResourceEntry(root, appbox::layout::kRegistryIsolationAppRelative),
                              registry_isolation.data(), registry_isolation.size(), error))
    {
        return error;
    }

    /*
     * The isolation modes of the virtual filesystem travel in the filesystem
     * domain, next to the layers they describe: the launcher hands the file to
     * the sandbox, which redirects the filesystem of the packaged application
     * through the modes. The launcher skips the file while it enumerates the
     * layers of that folder.
     */
    std::string filesystem_isolation;
    if (!appbox::BuildFilesystemIsolationFile(isolation, filesystem_isolation, error))
    {
        return error;
    }
    if (!writer.AddFileBuffer(ResourceEntry(root, appbox::layout::kFilesystemIsolationAppRelative),
                              filesystem_isolation.data(), filesystem_isolation.size(), error))
    {
        return error;
    }

    /*
     * The network configuration of the workspace travels in the network domain:
     * the launcher hands the file to the sandbox, which answers a name resolution
     * of the packaged application from the DNS redirections of the file instead
     * of asking the host and sends the traffic of the application through the
     * proxy of the file.
     */
    std::string network_isolation;
    if (!appbox::BuildNetworkIsolationFile(network, network_isolation, error))
    {
        return error;
    }
    if (!writer.AddFileBuffer(ResourceEntry(root, appbox::layout::kNetworkIsolationAppRelative),
                              network_isolation.data(), network_isolation.size(), error))
    {
        return error;
    }

    /*
     * The environment variables of the workspace travel in the environment
     * domain: the launcher hands the file to the sandbox, which composes the
     * environment of the packaged application from the entries while it starts
     * and keeps the modifications of the application inside the sandbox.
     */
    std::string environment_isolation;
    if (!appbox::BuildEnvironmentIsolationFile(environment, environment_isolation, error))
    {
        return error;
    }
    if (!writer.AddFileBuffer(ResourceEntry(root, appbox::layout::kEnvironmentIsolationAppRelative),
                              environment_isolation.data(), environment_isolation.size(), error))
    {
        return error;
    }

    /* Imported folders below the lower layer tree of the archive. */
    std::size_t done = 0;
    for (const auto& entry : appbox::PresetDirectories())
    {
        const auto imports = model.ImportsOf(entry.id);
        if (imports.empty())
        {
            continue;
        }

        const auto token = appbox::WideToUTF8(entry.layer_key);
        error = EnsureDirectory(writer, added, layer_prefix + "/" + token);
        if (!error.empty())
        {
            return error;
        }

        for (const auto& imported : imports)
        {
            const auto prefix = layer_prefix + "/" + token + "/" + appbox::WideToUTF8(imported.import_name);
            error = EnsureDirectory(writer, added, prefix);
            if (!error.empty())
            {
                return error;
            }

            error = AddImportedFolder(writer, imported.source_path, prefix, imported.import_name, added, done, total,
                                      progress);
            if (!error.empty())
            {
                return error;
            }
        }
    }

    /*
     * Folders the user created inside the workspace are entries of the layer of
     * their preset directory: they have no host counterpart, so the directory
     * itself is written. The layer root is written as well, because a preset
     * directory which holds such a folder does not have to hold an import.
     */
    for (const auto& directory : model.AllVirtualDirectories())
    {
        const auto owner = std::find_if(
            appbox::PresetDirectories().begin(), appbox::PresetDirectories().end(),
            [&directory](const appbox::PresetDirectory& entry) { return entry.id == directory.preset_id; });
        if (owner == appbox::PresetDirectories().end())
        {
            continue;
        }

        const auto layer = layer_prefix + "/" + appbox::WideToUTF8(owner->layer_key);
        error = EnsureDirectory(writer, added, layer);
        if (!error.empty())
        {
            return error;
        }

        error = EnsureDirectory(writer, added, layer + "/" + ToZipEntrySuffix(directory.target_dir));
        if (!error.empty())
        {
            return error;
        }
    }

    /* Imported files are written on top of the imported folders. */
    for (const auto& file : model.AllImportedFiles())
    {
        const auto owner =
            std::find_if(appbox::PresetDirectories().begin(), appbox::PresetDirectories().end(),
                         [&file](const appbox::PresetDirectory& entry) { return entry.id == file.preset_id; });
        if (owner == appbox::PresetDirectories().end())
        {
            continue;
        }

        std::string prefix = layer_prefix + "/" + appbox::WideToUTF8(owner->layer_key);
        error = EnsureDirectory(writer, added, prefix);
        if (!error.empty())
        {
            return error;
        }

        for (const auto& part : appbox::Split(file.target_dir, L"\\"))
        {
            if (part.empty())
            {
                continue;
            }
            prefix += "/" + appbox::WideToUTF8(part);
            error = EnsureDirectory(writer, added, prefix);
            if (!error.empty())
            {
                return error;
            }
        }

        done++;
        if (progress && !progress(appbox::BuildProgress{ appbox::BuildStage::Packing, done, total,
                                                         DisplayPath(file.target_dir, file.file_name) }))
        {
            return appbox::kBuildCancelledError;
        }

        if (!writer.AddFileDisk(file.source_path, prefix + "/" + appbox::WideToUTF8(file.file_name), error))
        {
            return error;
        }
    }

    /*
     * An entry the session hides travels as the whiteout marker of the entry:
     * the marker is a name of the view, so the layer only carries the
     * directory chain which holds it, and the sandbox hides the entry of the
     * host and of every layer below the marker while the packaged application
     * runs. The marker is an empty file, so it carries no content.
     */
    for (const auto& entry : model.AllWhiteouts())
    {
        const auto owner =
            std::find_if(appbox::PresetDirectories().begin(), appbox::PresetDirectories().end(),
                         [&entry](const appbox::PresetDirectory& preset) { return preset.id == entry.preset_id; });
        if (owner == appbox::PresetDirectories().end())
        {
            continue;
        }

        std::string prefix = layer_prefix + "/" + appbox::WideToUTF8(owner->layer_key);
        error = EnsureDirectory(writer, added, prefix);
        if (!error.empty())
        {
            return error;
        }

        for (const auto& part : appbox::Split(entry.target_dir, L"\\"))
        {
            if (part.empty())
            {
                continue;
            }
            prefix += "/" + appbox::WideToUTF8(part);
            error = EnsureDirectory(writer, added, prefix);
            if (!error.empty())
            {
                return error;
            }
        }

        const auto marker = appbox::filesystem_marker::WhiteoutNameOf(entry.name);

        done++;
        if (progress && !progress(appbox::BuildProgress{ appbox::BuildStage::Packing, done, total,
                                                         DisplayPath(entry.target_dir, marker) }))
        {
            return appbox::kBuildCancelledError;
        }

        /* An empty marker file: the buffer only has to be a valid pointer. */
        const char empty = 0;
        if (!writer.AddFileBuffer(prefix + "/" + appbox::WideToUTF8(marker), &empty, 0, error))
        {
            return error;
        }
    }

    return {};
}

/**
 * @brief Read the file properties the launcher of a run carries.
 *
 * The information is read from the source program of the session, which is the
 * program the metadata inherits from, and the fields the user edited are
 * applied on top of it. The read happens while the run is going on, so a
 * source program which was updated since the project was saved is picked up.
 *
 * A session without a source program still writes the fields the user edited,
 * and so does a session whose source program cannot be read: the file
 * properties of a project do not depend on the program being installed. The
 * reason why nothing was inherited is reported through @p warning in that
 * case, while the fields of the session are written all the same.
 *
 * @param[in] model The pack model.
 * @param[in] metadata Metadata of the session.
 * @param[out] info The information to write, empty when the session describes
 *             none.
 * @param[out] warning Reason why nothing was inherited, empty when the
 *             information was read in full.
 * @return true when there is information to write.
 */
bool ReadLauncherVersionInfo(const appbox::PackModel& model, const appbox::ApplicationMetadata& metadata,
                             appbox::ApplicationVersionInfo& info, std::string& warning)
{
    warning.clear();

    const auto source = appbox::MetadataSourcePath(model, metadata);
    if (source.empty())
    {
        info.fields = metadata.overrides;
        if (info.fields.empty())
        {
            warning = "the session has no program to inherit the file properties from";
            return false;
        }
        return true;
    }

    std::string read_error;
    if (appbox::ReadApplicationMetadata(source, info, read_error))
    {
        info.fields = appbox::MergeMetadataFields(info.fields, metadata.overrides);
        return true;
    }

    /* A source which cannot be read leaves the fields the user edited. */
    info = appbox::ApplicationVersionInfo{};
    info.fields = metadata.overrides;
    if (info.fields.empty())
    {
        warning = read_error;
        return false;
    }

    warning = read_error;
    return true;
}

/**
 * @brief Build the run configuration the launcher of the product carries.
 *
 * The layout of the product is a fixed convention, so the document only
 * carries what the launcher starts and how it presents the run. Every
 * executable path starts with the layer key token of its preset, which the
 * launcher expands.
 *
 * @param[in] model The pack model.
 * @param[in] startup Startup screen of the session.
 * @param[out] error Description of the failure, empty on success.
 * @return The configuration, meaningful only when @p error is empty.
 */
appbox::LauncherConfig BuildLauncherConfig(const appbox::PackModel& model, const appbox::StartupScreen& startup,
                                           std::string& error)
{
    error.clear();

    appbox::LauncherConfig config;

    /*
     * The identity of the sandbox: the launcher derives the name of its control
     * pipe from it, so every product of one session carries the same identity
     * and two sessions never do. The run only reads the identifier of the
     * session - an export is the only path which writes the project file - so a
     * pack run never leaves a half written file behind.
     */
    config.sandbox_id = model.EnsureSandboxId();

    /*
     * The startup screen of the session: the flag decides whether the launcher
     * opens a window of its own while it prepares the sandbox. The image is not
     * part of the document: it is embedded into the launcher payload as a
     * resource of its own, so a session which sets no image writes no resource
     * and the launcher shows the name of the application and the progress of
     * the run only.
     */
    config.startup.screen = startup.enabled;

    for (const auto& file : model.StartupFiles())
    {
        appbox::PresetDirectory owner;
        if (!appbox::FindPresetDirectory(file.preset_id, owner))
        {
            error = "the preset of a startup file no longer exists";
            return config;
        }

        auto launch = std::filesystem::path(owner.layer_key);
        launch /= file.import_name;
        launch /= file.relative_path;

        appbox::LauncherStartup launcher_startup;
        launcher_startup.trigger = appbox::WideToUTF8(file.trigger);
        launcher_startup.auto_start = file.auto_start;
        launcher_startup.executable = appbox::WideToUTF8(launch.wstring());
        config.startups.push_back(std::move(launcher_startup));
    }

    return config;
}

/**
 * @brief Build the program image of the product.
 *
 * The image is the launcher payload of the session with the icon and the file
 * properties of the packaged application applied, with the run configuration
 * and the startup image embedded as its resources and with the configuration of
 * the sandbox written into it, whose `hook_enabled` member is turned off, which
 * is what tells the injection module of the sandbox to leave the process alone.
 * Both products which carry a launcher write exactly this image: the archive of
 * `Pack()` as its launcher entry and the self-extracting program of
 * `PackSelfExtracting()` as the program itself.
 *
 * @param[in] model The pack model.
 * @param[in] metadata Metadata of the session.
 * @param[in] startup Startup screen of the session.
 * @param[in] payloads Payloads of the session, whose launcher bytes are the
 *            image to decorate.
 * @param[out] error Description of the failure, empty on success.
 * @return The patched image, empty when @p error is set.
 */
std::vector<char> BuildLauncherImage(const appbox::PackModel& model, const appbox::ApplicationMetadata& metadata,
                                     const appbox::StartupScreen& startup, const appbox::PackPayloads& payloads,
                                     std::string& error)
{
    error.clear();

    /*
     * The startup image is embedded into the launcher payload, so it is read
     * before the first entry is written: a session which names a file that is
     * not there is refused before the run starts writing, and the run reports
     * the file instead of writing a product whose launcher cannot show the
     * screen of the session.
     */
    {
        std::string startup_error;
        if (!appbox::ValidateStartupScreen(startup, startup_error))
        {
            error = startup_error;
            return {};
        }
    }

    std::vector<char> startup_image;
    if (appbox::HasStartupImage(startup))
    {
        std::string image_error;
        if (!appbox::pe_resource::ReadFileBytes(std::filesystem::path(startup.image), startup_image, image_error))
        {
            error = image_error;
            return {};
        }
    }

    const auto config = BuildLauncherConfig(model, startup, error);
    if (!error.empty())
    {
        return {};
    }

    /*
     * The launcher carries the file icon of the program the application
     * starts, so Explorer shows the icon of the packaged application for the
     * written program. A program without an icon never fails the run: the
     * launcher then keeps its own icon and the reason is logged.
     */
    std::wstring      startup_path;
    std::vector<char> payload_image;
    const auto* const icon_source = IconSourceOf(model.StartupFiles());
    if (icon_source != nullptr && model.StartupFilePath(*icon_source, startup_path))
    {
        std::string icon_warning;
        payload_image =
            appbox::ApplyApplicationIcon(payloads.launcher_bytes, payloads.launcher_size, startup_path, icon_warning);
        if (!icon_warning.empty())
        {
            spdlog::warn("the launcher keeps its own icon: {}", icon_warning);
        }
    }

    /*
     * The launcher carries the file properties of the packaged application as
     * well, which are the version information the shell shows on the `Details`
     * page of the written program. The information is applied to the image the
     * icon patch produced, and a patch which cannot be applied never fails the
     * run either: the payload keeps the state of the patch before it.
     */
    {
        const void* const base = payload_image.empty() ? payloads.launcher_bytes : payload_image.data();
        const auto        base_size = payload_image.empty() ? payloads.launcher_size : payload_image.size();

        appbox::ApplicationVersionInfo info;
        std::string                    metadata_warning;
        if (ReadLauncherVersionInfo(model, metadata, info, metadata_warning))
        {
            auto patched = appbox::ApplyApplicationMetadata(base, base_size, info, metadata_warning);
            if (!patched.empty())
            {
                payload_image = std::move(patched);
            }
        }

        if (!metadata_warning.empty())
        {
            spdlog::warn("the launcher keeps its own file properties: {}", metadata_warning);
        }
    }

    /*
     * The run configuration and the startup image of the session are resources
     * of the payload as well. They are written last, after the icon and the
     * version information, so a resource update of those patches cannot drop
     * them. A failure is fatal: a launcher without its configuration cannot
     * start the application.
     */
    {
        const void* const base = payload_image.empty() ? payloads.launcher_bytes : payload_image.data();
        const auto        base_size = payload_image.empty() ? payloads.launcher_size : payload_image.size();

        const std::string_view image_view =
            startup_image.empty() ? std::string_view() : std::string_view(startup_image.data(), startup_image.size());

        std::string config_error;
        auto        embedded = appbox::EmbedLauncherResources(base, base_size, appbox::BuildLauncherConfigJson(config),
                                                              image_view, config_error);
        if (embedded.empty())
        {
            error = "the launcher resources cannot be embedded: " + config_error;
            return {};
        }

        payload_image = std::move(embedded);
    }

    /*
     * The configuration of the sandbox is written last, after the resources: it
     * turns the hooks of the injection module off for this image, so a launcher
     * runs outside the sandbox of the application which started it (see
     * `src/core/SandboxConfigPayload.hpp`). A failure is fatal: a launcher
     * without the configuration would be isolated inside the sandbox which
     * started it, and it would refuse to serve a sandbox of its own.
     */
    {
        const void* const base = payload_image.empty() ? payloads.launcher_bytes : payload_image.data();
        const auto        base_size = payload_image.empty() ? payloads.launcher_size : payload_image.size();

        std::string payload_error;
        auto        patched = appbox::EmbedSandboxConfigPayload(base, base_size, payload_error);
        if (patched.empty())
        {
            error = "the sandbox configuration cannot be embedded: " + payload_error;
            return {};
        }

        payload_image = std::move(patched);
    }

    return payload_image;
}

/**
 * @brief Write a self-extracting program: image, payload archive and trailer.
 *
 * The payload is copied behind the image instead of being written at an offset
 * of an existing file, because libzip cannot write an archive at an offset of
 * another file.
 *
 * @param[in] exe_path Path of the program to write.
 * @param[in] image Program image to write first.
 * @param[in] payload Path of the payload archive to append.
 * @return Error description, empty on success.
 */
std::string WriteSelfExtractingProgram(const std::wstring& exe_path, const std::vector<char>& image,
                                       const std::filesystem::path& payload)
{
    const auto display = appbox::WideToUTF8(exe_path);

    std::ofstream out(exe_path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return "failed to create '" + display + "'";
    }

    out.write(image.data(), static_cast<std::streamsize>(image.size()));
    if (!out)
    {
        return "failed to write '" + display + "': the program image cannot be written";
    }

    std::uint64_t copied = 0;
    {
        std::ifstream in(payload, std::ios::binary);
        if (!in)
        {
            std::error_code ignored;
            std::filesystem::remove(exe_path, ignored);
            return "failed to write '" + display + "': the payload archive cannot be read";
        }

        std::vector<char> buffer(1024 * 1024);
        while (in)
        {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = in.gcount();
            if (read <= 0)
            {
                break;
            }

            out.write(buffer.data(), read);
            if (!out)
            {
                std::error_code ignored;
                std::filesystem::remove(exe_path, ignored);
                return "failed to write '" + display + "': the file cannot be written";
            }
            copied += static_cast<std::uint64_t>(read);
        }
    }

    const auto trailer =
        appbox::executable_payload::BuildTrailer(appbox::executable_payload::PayloadLocation{ image.size(), copied });
    out.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
    out.flush();

    if (!out)
    {
        std::error_code ignored;
        std::filesystem::remove(exe_path, ignored);
        return "failed to write '" + display + "': the file cannot be written";
    }

    return {};
}

} // namespace

namespace appbox
{

std::size_t CountFilesBelow(const std::wstring& folder)
{
    std::size_t count = 0;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_regular_file(ec))
        {
            count++;
        }
    }

    return count;
}

std::size_t ContentFileCount(const PackModel& model)
{
    /*
     * The whiteout markers are file entries of the archive as well, so they
     * are part of the total the progress reports announce.
     */
    std::size_t count = model.AllImportedFiles().size() + model.AllWhiteouts().size();
    for (const auto& entry : PresetDirectories())
    {
        for (const auto& imported : model.ImportsOf(entry.id))
        {
            count += CountFilesBelow(imported.source_path);
        }
    }

    return count;
}

std::wstring LauncherEntryName(const PackModel& model)
{
    if (!model.HasStartupFiles())
    {
        return {};
    }

    /*
     * Only the file name is used: the launcher lives in the archive root, which
     * is the single base filesystem of the extracted application.
     *
     * The launcher payload is a program image, so the entry always carries the
     * `.exe` extension, also while the first startup file is a script: the file
     * `run.bat` names the entry `run.exe`, which is the program a user of the
     * extracted archive sees.
     */
    const auto name = std::filesystem::path(model.StartupFiles().front().relative_path).filename();
    return name.stem().wstring() + L".exe";
}

std::string Pack(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                 const NetworkModel& network, const EnvironmentModel& environment, const ApplicationMetadata& metadata,
                 const StartupScreen& startup, const PackPayloads& payloads, CompressionLevel compression,
                 const std::wstring& zip_path, const BuildProgressCallback& progress)
{
    if (!model.HasStartupFiles())
    {
        return "no startup file selected";
    }
    if (!model.HasAutoStart())
    {
        return "at least one startup file must start automatically";
    }
    if (payloads.launcher_bytes == nullptr || payloads.launcher_size == 0)
    {
        return "the embedded launcher payload is empty";
    }

    /*
     * The startup image is embedded into the launcher payload, so it is read
     * before the archive is opened: a session which names a file that is not
     * there is refused before the first entry is added, and the run reports the
     * file instead of writing an archive whose launcher cannot show the screen
     * of the session.
     */
    std::string       image_error;
    std::vector<char> payload_image = BuildLauncherImage(model, metadata, startup, payloads, image_error);
    if (!image_error.empty())
    {
        return image_error;
    }

    /* Count the files once so the progress callback has a stable total. */
    const std::size_t total = kNonContentArchiveEntries + ContentFileCount(model);

    /*
     * The launcher payload and its configuration are added before the first
     * imported file, which is the preparing stage of the run.
     */
    if (progress && !progress(BuildProgress{ BuildStage::Preparing, 0, total, {} }))
    {
        return kBuildCancelledError;
    }

    try
    {
        ZipWriter   writer(zip_path, compression);
        std::string error;

        /*
         * The launcher executable itself, named after the first startup file:
         * the extracted archive shows the packaged application under its own
         * name instead of the launcher name.
         */
        const auto launcher_entry = WideToUTF8(LauncherEntryName(model));
        if (launcher_entry.empty())
        {
            return "no startup file selected";
        }

        if (!writer.AddFileBuffer(launcher_entry, payload_image.data(), payload_image.size(), error))
        {
            return error;
        }

        /*
         * The read-only resources of the packaged application live below
         * `app`, one directory per isolation domain: the filesystem layers and
         * their isolation modes, the virtual registry and its isolation modes,
         * the network configuration and the environment variables. The `data`
         * directory of the sandbox never travels in the archive: the launcher
         * creates it at run time, so deleting it resets the sandbox to the
         * state this archive carries.
         */
        error = WriteResourceTree(writer, layout::kAppDirName, model, registry, isolation, network, environment, total,
                                  progress);
        if (!error.empty())
        {
            return error;
        }

        if (!writer.Close(error))
        {
            return error;
        }
    }
    catch (const std::exception& e)
    {
        return std::string("failed to create the zip archive: ") + e.what();
    }

    if (progress)
    {
        /* The run is complete: report the final count without a current file. */
        progress(BuildProgress{ BuildStage::Packing, total, total, {} });
    }
    return {};
}

std::string PackSelfExtracting(const PackModel& model, const RegistryModel& registry,
                               const FilesystemIsolationModel& isolation, const NetworkModel& network,
                               const EnvironmentModel& environment, const ApplicationMetadata& metadata,
                               const StartupScreen& startup, const PackPayloads& payloads, CompressionLevel compression,
                               const std::wstring& exe_path, const BuildProgressCallback& progress)
{
    if (!model.HasStartupFiles())
    {
        return "no startup file selected";
    }
    if (!model.HasAutoStart())
    {
        return "at least one startup file must start automatically";
    }
    if (payloads.launcher_bytes == nullptr || payloads.launcher_size == 0)
    {
        return "the embedded launcher payload is empty";
    }

    std::string       image_error;
    std::vector<char> image = BuildLauncherImage(model, metadata, startup, payloads, image_error);
    if (!image_error.empty())
    {
        return image_error;
    }

    const std::size_t total = kNonContentExecutableEntries + ContentFileCount(model);
    if (progress && !progress(BuildProgress{ BuildStage::Preparing, 0, total, {} }))
    {
        return kBuildCancelledError;
    }

    try
    {
        /*
         * The payload archive is written first and appended afterwards, because
         * libzip cannot write an archive at an offset of another file. The
         * helper removes the temporary archive when the run ends.
         */
        const pe_resource::TemporaryFile payload_file(pe_resource::TemporaryPath(L"AppBox-Payload"));

        {
            ZipWriter   writer(payload_file.Path().wstring(), compression);
            std::string error;

            /*
             * The payload carries the resources below `app` only: the launcher
             * is the program itself, so the payload has neither a launcher entry
             * nor a `data` directory.
             */
            error = WriteResourceTree(writer, layout::kAppDirName, model, registry, isolation, network, environment,
                                      total, progress);
            if (!error.empty())
            {
                return error;
            }

            if (!writer.Close(error))
            {
                return error;
            }
        }

        const auto write_error = WriteSelfExtractingProgram(exe_path, image, payload_file.Path());
        if (!write_error.empty())
        {
            return write_error;
        }
    }
    catch (const std::exception& e)
    {
        return std::string("failed to create the self-extracting program: ") + e.what();
    }

    if (progress)
    {
        /* The run is complete: report the final count without a current file. */
        progress(BuildProgress{ BuildStage::Packing, total, total, {} });
    }
    return {};
}

std::string PackPatch(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                      const NetworkModel& network, const EnvironmentModel& environment, CompressionLevel compression,
                      const std::wstring& zip_path, const BuildProgressCallback& progress)
{
    /*
     * A patch package carries no launcher, so neither a startup file nor the
     * embedded payload is needed: the resources of the model are the whole
     * content of the package.
     */
    const std::size_t total = kNonContentPatchEntries + ContentFileCount(model);

    /* The run opens with the preparing stage, like a standalone pack run. */
    if (progress && !progress(BuildProgress{ BuildStage::Preparing, 0, total, {} }))
    {
        return kBuildCancelledError;
    }

    try
    {
        ZipWriter   writer(zip_path, compression);
        std::string error;

        /*
         * The resources are rooted at the archive root: the launcher of a
         * standalone archive merges the package into the resources below
         * `app`, so the package must not carry a resource directory of its own.
         */
        error = WriteResourceTree(writer, {}, model, registry, isolation, network, environment, total, progress);
        if (!error.empty())
        {
            return error;
        }

        if (!writer.Close(error))
        {
            return error;
        }
    }
    catch (const std::exception& e)
    {
        return std::string("failed to create the zip archive: ") + e.what();
    }

    if (progress)
    {
        /* The run is complete: report the final count without a current file. */
        progress(BuildProgress{ BuildStage::Packing, total, total, {} });
    }
    return {};
}

} // namespace appbox
