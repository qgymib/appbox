#ifndef APPBOX_COMMON_DETAILS_ZIP_READER_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_ZIP_READER_DETAIL_HPP

#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "ZipReaderError.hpp"
#include <zip.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace appbox
{
namespace detail
{

/** Size of the buffer used while streaming one entry to disk. */
inline constexpr std::size_t kZipReadBufferSize = 64 * 1024;

/**
 * @brief RAII wrapper closing a zip archive.
 */
struct ZipArchiveCloser
{
    /**
     * @brief Close the archive.
     * @param[in] archive Archive handle, may be nullptr.
     */
    void operator()(zip_t* archive) const
    {
        if (archive != nullptr)
        {
            zip_close(archive);
        }
    }
};

/**
 * @brief Sanitize one zip entry name into a relative host path.
 *
 * Forward slashes are converted to backslashes and empty segments are
 * dropped. Absolute paths, drive relative paths and parent references make
 * the entry unusable because they would escape the destination folder.
 *
 * @param[in] name Entry name of the archive.
 * @return The relative path, empty when the entry name is not safe.
 */
inline std::wstring SanitizeZipEntryName(const char* name)
{
    std::wstring converted;
    for (const auto ch : appbox::UTF8ToWide(name))
    {
        converted.push_back(ch == L'/' ? L'\\' : ch);
    }

    if (converted.empty() || converted.front() == L'\\' || (converted.size() >= 2 && converted[1] == L':'))
    {
        return {};
    }

    std::wstring result;
    for (const auto& part : appbox::Split(converted, L"\\"))
    {
        if (part.empty())
        {
            continue;
        }
        if (part == L"." || part == L"..")
        {
            return {};
        }

        if (!result.empty())
        {
            result.push_back(L'\\');
        }
        result += part;
    }

    return result;
}

/**
 * @brief Whether a zip entry name describes a directory.
 * @param[in] name Entry name of the archive.
 * @return true when the name carries the trailing slash of a directory.
 */
inline bool IsZipDirectoryEntry(const char* name)
{
    const auto length = std::strlen(name);
    return length > 0 && name[length - 1] == '/';
}

/**
 * @brief Build the path a progress report shows for one extracted entry.
 *
 * The content of the archive lives below `app\filesystem\<layer key>\`, a
 * prefix the user never chose; dropping those three segments makes the
 * reported path match the paths the packing stage reports. Entries of the
 * archive root, like the entry program and its configuration, and the
 * resources of the other domains keep their own name.
 *
 * @param[in] relative Sanitized path of the entry inside the archive.
 * @return The path to show in the progress report.
 */
inline std::wstring DisplayZipEntryName(const std::wstring& relative)
{
    const auto parts = appbox::Split(relative, L"\\");
    if (parts.size() < 4 || parts[0] != appbox::layout::kAppDirNameW || parts[1] != appbox::layout::kFilesystemDirNameW)
    {
        return relative;
    }

    std::wstring display;
    for (auto index = static_cast<std::size_t>(3); index < parts.size(); ++index)
    {
        if (!display.empty())
        {
            display.push_back(L'\\');
        }
        display += parts[index];
    }

    return display;
}

/**
 * @brief Write one zip entry to disk.
 * @param[in] archive Open archive.
 * @param[in] index Index of the entry.
 * @param[in] target Host path of the entry.
 * @return ZIPREADER_SUCCESS on success, the error code of the failed step otherwise.
 */
inline ZipReaderErrno WriteZipEntry(zip_t* archive, zip_uint64_t index, const std::filesystem::path& target)
{
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (file == nullptr)
    {
        return ZIPREADER_OPEN_ENTRY_FAILED;
    }

    FILE* handle = nullptr;
    if (_wfopen_s(&handle, target.wstring().c_str(), L"wb") != 0 || handle == nullptr)
    {
        zip_fclose(file);
        return ZIPREADER_CREATE_ENTRY_FAILED;
    }

    std::vector<char> buffer(kZipReadBufferSize);
    ZipReaderErrno    error = ZIPREADER_SUCCESS;
    for (;;)
    {
        const auto read = zip_fread(file, buffer.data(), buffer.size());
        if (read < 0)
        {
            error = ZIPREADER_READ_ENTRY_FAILED;
            break;
        }
        if (read == 0)
        {
            break;
        }

        const auto written = std::fwrite(buffer.data(), 1, static_cast<std::size_t>(read), handle);
        if (written != static_cast<std::size_t>(read))
        {
            error = ZIPREADER_WRITE_ENTRY_FAILED;
            break;
        }
    }

    std::fclose(handle);
    zip_fclose(file);
    return error;
}

} // namespace detail
} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_ZIP_READER_DETAIL_HPP
