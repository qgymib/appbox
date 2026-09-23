#include "PackService.hpp"
#include "ApplicationIcon.hpp"
#include "PresetDirectory.hpp"
#include "RegistryHive.hpp"
#include "RegistryIsolationFile.hpp"
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
std::string EnsureDirectory(appbox::ZipWriter& writer, std::set<std::string>& added,
                            const std::string& entry)
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
                              const std::wstring& display_root, std::set<std::string>& added,
                              std::size_t& done, std::size_t total,
                              const appbox::BuildProgressCallback& progress)
{
    const auto root = std::filesystem::path(source).lexically_normal();
    const auto root_string = root.wstring();

    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(root, ec);
    const auto end = std::filesystem::recursive_directory_iterator();
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
            if (progress
                && !progress(appbox::BuildProgress{appbox::BuildStage::Packing, done, total,
                                                   DisplayPath(display_root, relative)}))
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

std::wstring LoaderEntryName(const PackModel& model)
{
    if (!model.HasMainProgram())
    {
        return {};
    }

    /*
     * Only the file name is used: the loader lives in the archive root, which
     * is the single base filesystem of the extracted application.
     */
    return std::filesystem::path(model.MainProgramChoice().relative_path).filename().wstring();
}

std::string Pack(const PackModel& model, const RegistryModel& registry, const void* loader_bytes,
                 std::size_t loader_size, const std::wstring& zip_path, const BuildProgressCallback& progress)
{
    if (!model.HasMainProgram())
    {
        return "no main program selected";
    }
    if (loader_bytes == nullptr || loader_size == 0)
    {
        return "the embedded loader payload is empty";
    }

    PresetDirectory preset;
    if (!FindPresetDirectory(model.MainProgramChoice().preset_id, preset))
    {
        return "the preset of the main program no longer exists";
    }

    /* Count the files once so the progress callback has a stable total. */
    std::size_t total = kNonContentArchiveEntries + model.AllImportedFiles().size();
    for (const auto& entry : PresetDirectories())
    {
        for (const auto& imported : model.ImportsOf(entry.id))
        {
            total += CountFilesBelow(imported.source_path);
        }
    }
    /*
     * The loader payload and its configuration are added before the first
     * imported file, which is the preparing stage of the run.
     */
    if (progress && !progress(BuildProgress{BuildStage::Preparing, 0, total, {}}))
    {
        return kBuildCancelledError;
    }

    try
    {
        ZipWriter writer(zip_path);
        std::string error;

        /*
         * The loader executable itself, named after the main program: the
         * extracted archive shows the packaged application under its own name
         * instead of the loader name.
         */
        const auto loader_entry = WideToUTF8(LoaderEntryName(model));
        if (loader_entry.empty())
        {
            return "no main program selected";
        }

        /*
         * The loader carries the file icon of the main program, so Explorer
         * shows the icon of the packaged application for the extracted
         * program. A program without an icon never fails the pack run: the
         * loader then keeps its own icon and the reason is logged.
         */
        std::wstring      main_program_path;
        std::vector<char> patched_loader;
        if (model.MainProgramPath(main_program_path))
        {
            std::string icon_warning;
            patched_loader =
                ApplyApplicationIcon(loader_bytes, loader_size, main_program_path, icon_warning);
            if (!icon_warning.empty())
            {
                spdlog::warn("the loader keeps its own icon: {}", icon_warning);
            }
        }

        const void* const payload = patched_loader.empty() ? loader_bytes : patched_loader.data();
        const auto payload_size = patched_loader.empty() ? loader_size : patched_loader.size();
        if (!writer.AddFileBuffer(loader_entry, payload, payload_size, error))
        {
            return error;
        }

        /*
         * Loader configuration: the archive root is the single base
         * filesystem, the overlay lives beside it, and the launch path is
         * the layer key token of the preset expanded by the loader. The
         * config file carries the name of the loader executable, which loads
         * `<own file name>.json` from its own directory.
         */
        LoaderConfig config;
        config.base_fs.push_back(".");
        config.overlay_fs = "data";

        auto launch = std::filesystem::path(preset.layer_key);
        launch /= model.MainProgramChoice().import_name;
        launch /= model.MainProgramChoice().relative_path;
        config.launch.executable = WideToUTF8(launch.wstring());

        const auto json = nlohmann::json(config).dump(2);
        if (!writer.AddFileBuffer(loader_entry + ".json", json.data(), json.size(), error))
        {
            return error;
        }

        std::set<std::string> added;

        /*
         * The registry of the workspace travels inside the overlay: the hive
         * holds the virtual registry the sandbox mounts, the isolation file
         * holds the modes which decide which host entries stay visible. Both
         * live in the registry folder of the overlay, which is exactly where
         * the loader looks for them at run time.
         */
        const std::string registry_prefix = config.overlay_fs + "/registry";
        error = EnsureDirectory(writer, added, config.overlay_fs);
        if (!error.empty())
        {
            return error;
        }
        error = EnsureDirectory(writer, added, registry_prefix);
        if (!error.empty())
        {
            return error;
        }

        std::vector<std::uint8_t> hive;
        if (!BuildRegistryHiveBytes(registry, hive, error))
        {
            return error;
        }
        if (!writer.AddFileBuffer(registry_prefix + "/user.hiv", hive.data(), hive.size(), error))
        {
            return error;
        }

        std::string isolation;
        if (!BuildRegistryIsolationFile(registry, isolation, error))
        {
            return error;
        }
        if (!writer.AddFileBuffer(registry_prefix + "/isolation.json", isolation.data(), isolation.size(), error))
        {
            return error;
        }

        /* Imported folders below the lower layer tree of the archive. */
        error = EnsureDirectory(writer, added, "filesystem");
        if (!error.empty())
        {
            return error;
        }

        std::size_t done = 0;
        for (const auto& entry : PresetDirectories())
        {
            const auto imports = model.ImportsOf(entry.id);
            if (imports.empty())
            {
                continue;
            }

            const auto token = WideToUTF8(entry.layer_key);
            error = EnsureDirectory(writer, added, "filesystem/" + token);
            if (!error.empty())
            {
                return error;
            }

            for (const auto& imported : imports)
            {
                const auto prefix = "filesystem/" + token + "/" + WideToUTF8(imported.import_name);
                error = EnsureDirectory(writer, added, prefix);
                if (!error.empty())
                {
                    return error;
                }

                error = AddImportedFolder(writer, imported.source_path, prefix, imported.import_name, added,
                                          done, total, progress);
                if (!error.empty())
                {
                    return error;
                }
            }
        }

        /* Imported files are written on top of the imported folders. */
        for (const auto& file : model.AllImportedFiles())
        {
            const auto owner = std::find_if(
                PresetDirectories().begin(), PresetDirectories().end(),
                [&file](const PresetDirectory& entry) { return entry.id == file.preset_id; });
            if (owner == PresetDirectories().end())
            {
                continue;
            }

            std::string prefix = "filesystem/" + WideToUTF8(owner->layer_key);
            error = EnsureDirectory(writer, added, prefix);
            if (!error.empty())
            {
                return error;
            }

            for (const auto& part : Split(file.target_dir, L"\\"))
            {
                if (part.empty())
                {
                    continue;
                }
                prefix += "/" + WideToUTF8(part);
                error = EnsureDirectory(writer, added, prefix);
                if (!error.empty())
                {
                    return error;
                }
            }

            done++;
            if (progress
                && !progress(BuildProgress{BuildStage::Packing, done, total,
                                           DisplayPath(file.target_dir, file.file_name)}))
            {
                return kBuildCancelledError;
            }

            if (!writer.AddFileDisk(file.source_path, prefix + "/" + WideToUTF8(file.file_name), error))
            {
                return error;
            }
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
        progress(BuildProgress{BuildStage::Packing, total, total, {}});
    }
    return {};
}

} // namespace appbox
