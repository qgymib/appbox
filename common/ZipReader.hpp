#ifndef APPBOX_COMMON_ZIP_READER_HPP
#define APPBOX_COMMON_ZIP_READER_HPP

#include "ZipReaderError.hpp"
#include "details/ZipReaderDetail.hpp"
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>

namespace appbox
{

/**
 * @brief File level progress of an extraction.
 *
 * The report carries the file which is being extracted right now, so a caller
 * can name it instead of only showing a counter. The extraction has a single
 * stage, so the report does not name one.
 */
struct ExtractProgress
{
    /**
     * @brief Number of files written so far.
     */
    std::size_t done = 0;

    /**
     * @brief Total number of files of the archive.
     */
    std::size_t total = 0;

    /**
     * @brief Path of the file which is being extracted, relative to the
     *        destination folder.
     *
     * An empty path means that no single file is being handled, which is the
     * case for the report which opens the run.
     */
    std::wstring current;
};

/**
 * @brief Progress callback of an extraction.
 *
 * The callback is invoked once per extracted file; returning false aborts the
 * extraction with ZIPREADER_CANCELLED.
 */
using ExtractProgressCallback = std::function<bool(const ExtractProgress&)>;

/**
 * @brief Extract a zip archive into a destination folder.
 *
 * The destination folder is created when it does not exist, and the entry
 * names of the archive are sanitized: absolute paths, drive relative paths
 * and parent references are rejected, so an archive can never write outside
 * of the destination folder. Directory entries become folders, which keeps
 * empty folders of the archive intact.
 *
 * The archive is written by ZipWriter of the packer, so the two functions are
 * the write and read half of the same archive layout.
 *
 * Every extracted file is reported through the progress callback. Directory
 * entries only recreate the folder structure and are not reported, so the
 * total matches the number of file entries of the archive. The reported path
 * drops the `app/filesystem/<layer key>` prefix of the archive layout, which
 * makes it match the paths the packing stage reports.
 *
 * @param[in] zip_path Archive to extract.
 * @param[in] dest_dir Destination folder.
 * @param[in] progress Called once per extracted file; returning false aborts
 *                     the extraction with ZIPREADER_CANCELLED. May be empty
 *                     to disable progress reporting.
 * @return ZIPREADER_SUCCESS on success, the error code of the failed step
 *         otherwise.
 */
inline ZipReaderErrno ExtractArchive(const std::wstring& zip_path, const std::wstring& dest_dir,
                                     const ExtractProgressCallback& progress = {})
{
    const auto path = WideToUTF8(zip_path);

    int                                                    error_code = 0;
    const std::unique_ptr<zip_t, detail::ZipArchiveCloser> archive(zip_open(path.c_str(), ZIP_RDONLY, &error_code),
                                                                   detail::ZipArchiveCloser{});
    if (archive == nullptr)
    {
        return ZIPREADER_OPEN_ARCHIVE_FAILED;
    }

    const auto root = std::filesystem::path(dest_dir).lexically_normal();

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        return ZIPREADER_CREATE_DESTINATION_FAILED;
    }

    const auto count = zip_get_num_entries(archive.get(), 0);

    /*
     * The progress total is the number of file entries: directory entries only
     * recreate the folder structure, which matches the packing stage where
     * only regular files are counted.
     */
    std::size_t total = 0;
    for (auto index = static_cast<zip_uint64_t>(0); index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat = {};
        if (zip_stat_index(archive.get(), index, 0, &stat) == 0 && stat.name != nullptr &&
            (stat.valid & ZIP_STAT_NAME) != 0 && !detail::IsZipDirectoryEntry(stat.name))
        {
            total++;
        }
    }

    if (progress && !progress(ExtractProgress{ 0, total, {} }))
    {
        return ZIPREADER_CANCELLED;
    }

    std::size_t done = 0;
    for (auto index = static_cast<zip_uint64_t>(0); index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat = {};
        if (zip_stat_index(archive.get(), index, 0, &stat) < 0)
        {
            return ZIPREADER_READ_ENTRY_FAILED;
        }
        if (stat.name == nullptr || (stat.valid & ZIP_STAT_NAME) == 0)
        {
            continue;
        }

        std::wstring relative;
        try
        {
            relative = detail::SanitizeZipEntryName(stat.name);
        }
        catch (const std::exception&)
        {
            return ZIPREADER_UNREADABLE_ENTRY_NAME;
        }

        if (relative.empty())
        {
            return ZIPREADER_UNSAFE_ENTRY_NAME;
        }

        const auto target = root / relative;
        if (detail::IsZipDirectoryEntry(stat.name))
        {
            std::filesystem::create_directories(target, ec);
            if (ec)
            {
                return ZIPREADER_CREATE_ENTRY_FAILED;
            }
            continue;
        }

        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            return ZIPREADER_CREATE_ENTRY_FAILED;
        }

        /*
         * The entry is reported before it is written, so the dialog names the
         * file while it is being extracted instead of after the fact.
         */
        done++;
        if (progress && !progress(ExtractProgress{ done, total, detail::DisplayZipEntryName(relative) }))
        {
            return ZIPREADER_CANCELLED;
        }

        const auto error = detail::WriteZipEntry(archive.get(), index, target);
        if (error != ZIPREADER_SUCCESS)
        {
            return error;
        }
    }

    return ZIPREADER_SUCCESS;
}

} // namespace appbox

#endif // APPBOX_COMMON_ZIP_READER_HPP
