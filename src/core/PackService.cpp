#include "PackService.hpp"
#include "ApplicationIcon.hpp"
#include "EnvironmentIsolationFile.hpp"
#include "FilesystemIsolationFile.hpp"
#include "NetworkIsolationFile.hpp"
#include "PresetDirectory.hpp"
#include "RegistryHive.hpp"
#include "RegistryIsolationFile.hpp"
#include "SandboxLayout.hpp"
#include "ZipWriter.hpp"
#include "Config.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
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
 * loader creates it at run time, so deleting it resets the sandbox to the
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
     * isolation file. The loader seeds the hive into the state directory of the
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
     * domain, next to the layers they describe: the loader hands the file to
     * the sandbox, which redirects the filesystem of the packaged application
     * through the modes. The loader skips the file while it enumerates the
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
     * the loader hands the file to the sandbox, which answers a name resolution
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
     * domain: the loader hands the file to the sandbox, which composes the
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
    std::size_t count = model.AllImportedFiles().size();
    for (const auto& entry : PresetDirectories())
    {
        for (const auto& imported : model.ImportsOf(entry.id))
        {
            count += CountFilesBelow(imported.source_path);
        }
    }

    return count;
}

std::wstring LoaderEntryName(const PackModel& model)
{
    if (!model.HasStartupFiles())
    {
        return {};
    }

    /*
     * Only the file name is used: the loader lives in the archive root, which
     * is the single base filesystem of the extracted application.
     */
    return std::filesystem::path(model.StartupFiles().front().relative_path).filename().wstring();
}

std::string Pack(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                 const NetworkModel& network, const EnvironmentModel& environment, const PackPayloads& payloads,
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
    if (payloads.loader_bytes == nullptr || payloads.loader_size == 0)
    {
        return "the embedded loader payload is empty";
    }
    if (payloads.sandbox32_bytes == nullptr || payloads.sandbox32_size == 0)
    {
        return "the embedded 32 bit sandbox module is empty";
    }
    if (payloads.sandbox64_bytes == nullptr || payloads.sandbox64_size == 0)
    {
        return "the embedded 64 bit sandbox module is empty";
    }

    /* Count the files once so the progress callback has a stable total. */
    const std::size_t total = kNonContentArchiveEntries + ContentFileCount(model);

    /*
     * The loader payload and its configuration are added before the first
     * imported file, which is the preparing stage of the run.
     */
    if (progress && !progress(BuildProgress{ BuildStage::Preparing, 0, total, {} }))
    {
        return kBuildCancelledError;
    }

    try
    {
        ZipWriter   writer(zip_path);
        std::string error;

        /*
         * The loader executable itself, named after the first startup file:
         * the extracted archive shows the packaged application under its own
         * name instead of the loader name.
         */
        const auto loader_entry = WideToUTF8(LoaderEntryName(model));
        if (loader_entry.empty())
        {
            return "no startup file selected";
        }

        /*
         * The loader carries the file icon of the first startup file, so
         * Explorer shows the icon of the packaged application for the
         * extracted program. A program without an icon never fails the pack
         * run: the loader then keeps its own icon and the reason is logged.
         */
        std::wstring      startup_path;
        std::vector<char> patched_loader;
        if (model.StartupFilePath(model.StartupFiles().front(), startup_path))
        {
            std::string icon_warning;
            patched_loader =
                ApplyApplicationIcon(payloads.loader_bytes, payloads.loader_size, startup_path, icon_warning);
            if (!icon_warning.empty())
            {
                spdlog::warn("the loader keeps its own icon: {}", icon_warning);
            }
        }

        const void* const payload = patched_loader.empty() ? payloads.loader_bytes : patched_loader.data();
        const auto        payload_size = patched_loader.empty() ? payloads.loader_size : patched_loader.size();
        if (!writer.AddFileBuffer(loader_entry, payload, payload_size, error))
        {
            return error;
        }

        /*
         * Loader configuration: the layout of the archive is a fixed
         * convention, so the file only carries the startup files and the
         * environment of the packaged application. Every executable path
         * starts with the layer key token of its preset, which the loader
         * expands. The config file carries the name of the loader executable,
         * which loads `<own file name>.json` from its own directory.
         */
        LoaderConfig config;

        for (const auto& file : model.StartupFiles())
        {
            PresetDirectory owner;
            if (!FindPresetDirectory(file.preset_id, owner))
            {
                return "the preset of a startup file no longer exists";
            }

            auto launch = std::filesystem::path(owner.layer_key);
            launch /= file.import_name;
            launch /= file.relative_path;

            LoaderStartup startup;
            startup.trigger = WideToUTF8(file.trigger);
            startup.auto_start = file.auto_start;
            startup.executable = WideToUTF8(launch.wstring());
            config.startups.push_back(std::move(startup));
        }

        const auto json = nlohmann::json(config).dump(2);
        if (!writer.AddFileBuffer(loader_entry + ".json", json.data(), json.size(), error))
        {
            return error;
        }

        /*
         * The read-only resources of the packaged application live below
         * `app`, one directory per isolation domain: the filesystem layers and
         * their isolation modes, the virtual registry and its isolation modes,
         * the network configuration and the environment variables. The `data`
         * directory of the sandbox never travels in the archive: the loader
         * creates it at run time, so deleting it resets the sandbox to the
         * state this archive carries.
         */
        error = WriteResourceTree(writer, layout::kAppDirName, model, registry, isolation, network, environment, total,
                                  progress);
        if (!error.empty())
        {
            return error;
        }

        /*
         * The sandbox injection modules are resources of the archive, so they
         * land beside the four isolation domains instead of in the state
         * directory of the sandbox: the loader injects the module of the
         * bitness of the process it starts from `app`, which keeps a run of the
         * extracted archive from copying a module of its own.
         */
        if (!writer.AddFileBuffer(ResourceEntry(layout::kAppDirName, layout::kSandbox32DllName),
                                  payloads.sandbox32_bytes, payloads.sandbox32_size, error))
        {
            return error;
        }
        if (!writer.AddFileBuffer(ResourceEntry(layout::kAppDirName, layout::kSandbox64DllName),
                                  payloads.sandbox64_bytes, payloads.sandbox64_size, error))
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

std::string PackPatch(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                      const NetworkModel& network, const EnvironmentModel& environment, const std::wstring& zip_path,
                      const BuildProgressCallback& progress)
{
    /*
     * A patch package carries no loader, so neither a startup file nor the
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
        ZipWriter   writer(zip_path);
        std::string error;

        /*
         * The resources are rooted at the archive root: the loader of a
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
