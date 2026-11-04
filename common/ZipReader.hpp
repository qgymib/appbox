#ifndef APPBOX_COMMON_ZIP_READER_HPP
#define APPBOX_COMMON_ZIP_READER_HPP

#include "ErrorCode.hpp"
#include "WString.hpp"
#include <tl/expected.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
 * extraction with `kZipCancelled`.
 */
using ExtractProgressCallback = std::function<bool(const ExtractProgress&)>;

} // namespace appbox

/*
 * The helpers of the extraction are defined in their own header, which needs the
 * declaration of the progress report and of the callback above.
 */
#include "details/ZipReaderDetail.hpp"

namespace appbox
{

namespace detail
{

/**
 * @brief Extract every entry of an opened archive into a destination folder.
 *
 * The archive is owned by the caller, so both the path variant and the
 * embedded region variant share this body.
 *
 * A callback which throws is refused with `kInvalidState`: the extraction is a
 * `noexcept` interface, and a callback which breaks its contract must not turn
 * the answer of the extraction into a call of `std::terminate()`.
 *
 * @param[in] archive Opened archive.
 * @param[in] dest_dir Destination folder.
 * @param[in] progress Called once per extracted file; may be empty.
 * @return The success value, or the code of the failed step.
 */
inline tl::expected<void, ErrorCode> ExtractArchiveEntries(zip_t* archive, const std::wstring& dest_dir,
                                                           const ExtractProgressCallback& progress) noexcept
{
    const auto root = std::filesystem::path(dest_dir).lexically_normal();

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kZipCreateDestinationFailed);
    }

    const auto count = zip_get_num_entries(archive, 0);

    /*
     * The progress total is the number of file entries: directory entries only
     * recreate the folder structure, which matches the packing stage where
     * only regular files are counted.
     */
    std::size_t total = 0;
    for (auto index = static_cast<zip_uint64_t>(0); index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat = {};
        if (zip_stat_index(archive, index, 0, &stat) == 0 && stat.name != nullptr &&
            (stat.valid & ZIP_STAT_NAME) != 0 && !detail::IsZipDirectoryEntry(stat.name))
        {
            total++;
        }
    }

    /*
     * The callback is the only caller code this body invokes, so it is the only
     * place which catches: a callback which throws breaks its contract, and the
     * code of that failure is answered instead of letting the exception escape
     * a `noexcept` interface. A callback which answers false abandons the
     * extraction, which is the cancellation of the run.
     */
    const auto report = [&progress](const ExtractProgress& state) noexcept -> std::optional<bool> {
        if (!progress)
        {
            return true;
        }

        try
        {
            return progress(state);
        }
        catch (...)
        {
            return std::nullopt;
        }
    };

    const auto opening = report(ExtractProgress{ 0, total, {} });
    if (!opening)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kInvalidState);
    }
    if (!*opening)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kZipCancelled);
    }

    std::size_t done = 0;
    for (auto index = static_cast<zip_uint64_t>(0); index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat = {};
        if (zip_stat_index(archive, index, 0, &stat) < 0)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kZipReadEntryFailed);
        }
        if (stat.name == nullptr || (stat.valid & ZIP_STAT_NAME) == 0)
        {
            continue;
        }

        const auto sanitized = detail::SanitizeZipEntryName(stat.name);
        if (!sanitized)
        {
            return tl::unexpected<ErrorCode>(sanitized.error());
        }

        const auto& relative = *sanitized;
        const auto  target = root / relative;
        if (detail::IsZipDirectoryEntry(stat.name))
        {
            std::filesystem::create_directories(target, ec);
            if (ec)
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kZipCreateEntryFailed);
            }
            continue;
        }

        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kZipCreateEntryFailed);
        }

        /*
         * The entry is reported before it is written, so the dialog names the
         * file while it is being extracted instead of after the fact.
         */
        done++;
        const auto accepted = report(ExtractProgress{ done, total, detail::DisplayZipEntryName(relative) });
        if (!accepted)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kInvalidState);
        }
        if (!*accepted)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kZipCancelled);
        }

        const auto written = detail::WriteZipEntry(archive, index, target);
        if (!written)
        {
            return tl::unexpected<ErrorCode>(written.error());
        }
    }

    return {};
}

} // namespace detail

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
 *                     the extraction with `kZipCancelled`. May be empty to
 *                     disable progress reporting.
 * @return The success value, or the code of the failed step.
 */
inline tl::expected<void, ErrorCode> ExtractArchive(const std::wstring& zip_path, const std::wstring& dest_dir,
                                                    const ExtractProgressCallback& progress = {}) noexcept
{
    /* The archive is opened by the library through a UTF-8 path, so a path
     * which the code page cannot carry is the code of the conversion. */
    const auto converted_path = WideToUTF8(zip_path);
    if (!converted_path)
    {
        return tl::unexpected<ErrorCode>(converted_path.error());
    }
    const std::string& path = *converted_path;

    int                                                    error_code = 0;
    const std::unique_ptr<zip_t, detail::ZipArchiveCloser> archive(zip_open(path.c_str(), ZIP_RDONLY, &error_code),
                                                                   detail::ZipArchiveCloser{});
    if (archive == nullptr)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kZipOpenFailed);
    }

    return detail::ExtractArchiveEntries(archive.get(), dest_dir, progress);
}

/**
 * @brief Extract a zip archive which is embedded in another file.
 *
 * A self-extracting program carries its payload behind its own image, so the
 * archive does not start at offset zero of the file. libzip cannot open such a
 * file by path, but a source which describes the region does present the bytes
 * as a standalone archive.
 *
 * @param[in] path File which carries the archive.
 * @param[in] offset Offset of the archive inside that file, in bytes.
 * @param[in] length Length of the archive region, in bytes.
 * @param[in] dest_dir Destination folder.
 * @param[in] progress Called once per extracted file; may be empty.
 * @return The success value, or the code of the failed step.
 */
inline tl::expected<void, ErrorCode> ExtractArchiveAt(const std::wstring& path, std::uint64_t offset,
                                                      std::uint64_t length, const std::wstring& dest_dir,
                                                      const ExtractProgressCallback& progress = {}) noexcept
{
    /* The region is described by a UTF-8 path as well, so a path which the code
     * page cannot carry is the code of the conversion. */
    const auto converted_path = WideToUTF8(path);
    if (!converted_path)
    {
        return tl::unexpected<ErrorCode>(converted_path.error());
    }
    const std::string& host_path = *converted_path;

    zip_error_t error;
    zip_error_init(&error);

    zip_source_t* source = zip_source_file_create(host_path.c_str(), offset, static_cast<zip_int64_t>(length), &error);
    if (source == nullptr)
    {
        zip_error_fini(&error);
        return tl::unexpected<ErrorCode>(ErrorCode::kZipOpenFailed);
    }

    zip_t* opened = zip_open_from_source(source, ZIP_RDONLY, &error);
    if (opened == nullptr)
    {
        /* zip_open_from_source() does not take the source on failure. */
        zip_source_free(source);
        zip_error_fini(&error);
        return tl::unexpected<ErrorCode>(ErrorCode::kZipOpenFailed);
    }

    zip_error_fini(&error);

    const std::unique_ptr<zip_t, detail::ZipArchiveCloser> archive(opened, detail::ZipArchiveCloser{});
    return detail::ExtractArchiveEntries(archive.get(), dest_dir, progress);
}

} // namespace appbox

#endif // APPBOX_COMMON_ZIP_READER_HPP
