#include "ZipReader.hpp"
#include "WString.hpp"
#include <zip.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

namespace
{

/** Size of the buffer used while streaming one entry to disk. */
constexpr std::size_t kReadBufferSize = 64 * 1024;

/**
 * @brief RAII wrapper closing a zip archive.
 */
struct ArchiveCloser
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
 * @brief Format the pending libzip error of an open archive.
 * @param[in] archive Open archive.
 * @param[in] context Description of the failed operation.
 * @return The composed error message.
 */
std::string FormatError(zip_t* archive, const char* context)
{
    std::string message(context);
    const char* detail = zip_strerror(archive);
    if (detail != nullptr)
    {
        message += ": ";
        message += detail;
    }
    return message;
}

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
std::wstring SanitizeEntryName(const char* name)
{
    std::wstring converted;
    for (const auto ch : appbox::UTF8ToWide(name))
    {
        converted.push_back(ch == L'/' ? L'\\' : ch);
    }

    if (converted.empty()
        || converted.front() == L'\\'
        || (converted.size() >= 2 && converted[1] == L':'))
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
bool IsDirectoryEntry(const char* name)
{
    const auto length = std::strlen(name);
    return length > 0 && name[length - 1] == '/';
}

/**
 * @brief Write one zip entry to disk.
 * @param[in] archive Open archive.
 * @param[in] index Index of the entry.
 * @param[in] target Host path of the entry.
 * @return Error description, empty on success.
 */
std::string WriteEntry(zip_t* archive, zip_uint64_t index, const std::filesystem::path& target)
{
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (file == nullptr)
    {
        return FormatError(archive, "failed to open an entry of the archive");
    }

    FILE* handle = nullptr;
    if (_wfopen_s(&handle, target.wstring().c_str(), L"wb") != 0 || handle == nullptr)
    {
        zip_fclose(file);
        return "failed to create '" + appbox::WideToUTF8(target.wstring()) + "'";
    }

    std::vector<char> buffer(kReadBufferSize);
    std::string error;
    for (;;)
    {
        const auto read = zip_fread(file, buffer.data(), buffer.size());
        if (read < 0)
        {
            error = FormatError(archive, "failed to read an entry of the archive");
            break;
        }
        if (read == 0)
        {
            break;
        }

        const auto written = std::fwrite(buffer.data(), 1, static_cast<std::size_t>(read), handle);
        if (written != static_cast<std::size_t>(read))
        {
            error = "failed to write '" + appbox::WideToUTF8(target.wstring()) + "'";
            break;
        }
    }

    std::fclose(handle);
    zip_fclose(file);
    return error;
}

} // namespace

namespace appbox
{

std::string ExtractArchive(const std::wstring& zip_path, const std::wstring& dest_dir)
{
    const auto path = WideToUTF8(zip_path);

    int error_code = 0;
    const std::unique_ptr<zip_t, ArchiveCloser> archive(
        zip_open(path.c_str(), ZIP_RDONLY, &error_code), ArchiveCloser{});
    if (archive == nullptr)
    {
        zip_error_t detail;
        zip_error_init(&detail);
        zip_error_set(&detail, error_code, errno);
        const std::string message(zip_error_strerror(&detail));
        zip_error_fini(&detail);
        return "failed to open the archive '" + path + "': " + message;
    }

    const auto root = std::filesystem::path(dest_dir).lexically_normal();

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        return "failed to create the destination folder '" + WideToUTF8(dest_dir) + "'";
    }

    const auto count = zip_get_num_entries(archive.get(), 0);
    for (auto index = static_cast<zip_uint64_t>(0); index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat = {};
        if (zip_stat_index(archive.get(), index, 0, &stat) < 0)
        {
            return FormatError(archive.get(), "failed to read an entry of the archive");
        }
        if (stat.name == nullptr || (stat.valid & ZIP_STAT_NAME) == 0)
        {
            continue;
        }

        std::wstring relative;
        try
        {
            relative = SanitizeEntryName(stat.name);
        }
        catch (const std::exception& e)
        {
            return std::string("the archive contains an unreadable entry name: ") + e.what();
        }

        if (relative.empty())
        {
            return std::string("the archive contains an unsafe entry name: ") + stat.name;
        }

        const auto target = root / relative;
        if (IsDirectoryEntry(stat.name))
        {
            std::filesystem::create_directories(target, ec);
            if (ec)
            {
                return "failed to create '" + WideToUTF8(target.wstring()) + "'";
            }
            continue;
        }

        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            return "failed to create '" + WideToUTF8(target.parent_path().wstring()) + "'";
        }

        const auto error = WriteEntry(archive.get(), index, target);
        if (!error.empty())
        {
            return error;
        }
    }

    return {};
}

} // namespace appbox
