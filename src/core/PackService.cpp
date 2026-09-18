#include "PackService.hpp"
#include "ApplicationIcon.hpp"
#include "PresetDirectory.hpp"
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
 * @brief Recursively add one imported folder below a zip prefix.
 *
 * Every visited directory becomes a directory entry, so empty folders are
 * preserved. Regular files are streamed from disk.
 *
 * @param[in,out] writer The zip writer.
 * @param[in] source Host folder to add.
 * @param[in] prefix Zip entry prefix of the import, e.g.
 *                   `"filesystem/#ProgramFiles#/MyApp"`.
 * @param[in,out] added Directory entries which were already added.
 * @param[in,out] done Number of files already packed.
 * @param[in] total Total number of files to pack.
 * @param[in] progress Progress callback, may be empty.
 * @return Error description, empty on success.
 */
std::string AddImportedFolder(appbox::ZipWriter& writer, const std::wstring& source, const std::string& prefix,
                              std::set<std::string>& added, std::size_t& done, std::size_t total,
                              const std::function<bool(std::size_t, std::size_t)>& progress)
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
            if (!ec)
            {
                std::string error;
                if (!writer.AddFileDisk(full, entry, error))
                {
                    return error;
                }
            }

            done++;
            if (progress && !progress(done, total))
            {
                return appbox::kPackCancelledError;
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

std::string Pack(const PackModel& model, const void* loader_bytes, std::size_t loader_size,
                 const std::wstring& zip_path,
                 const std::function<bool(std::size_t, std::size_t)>& progress)
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
    std::size_t total = model.AllImportedFiles().size();
    for (const auto& entry : PresetDirectories())
    {
        for (const auto& imported : model.ImportsOf(entry.id))
        {
            total += CountFilesBelow(imported.source_path);
        }
    }
    if (progress && !progress(0, total))
    {
        return kPackCancelledError;
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

        /* Imported folders below the lower layer tree of the archive. */
        std::set<std::string> added;
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

                error = AddImportedFolder(writer, imported.source_path, prefix, added, done, total, progress);
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

            if (!writer.AddFileDisk(file.source_path, prefix + "/" + WideToUTF8(file.file_name), error))
            {
                return error;
            }

            done++;
            if (progress && !progress(done, total))
            {
                return kPackCancelledError;
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
        progress(total, total);
    }
    return {};
}

} // namespace appbox
