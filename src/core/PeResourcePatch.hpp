#ifndef APPBOX_PACKER_CORE_PE_RESOURCE_PATCH_HPP
#define APPBOX_PACKER_CORE_PE_RESOURCE_PATCH_HPP

/*
 * The resource API is used with the wide character forms: the RT_ICON and
 * RT_GROUP_ICON macros expand to their ANSI form unless UNICODE is defined, and
 * the targets of the packer do not define it.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include "WString.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

/**
 * @brief Helpers which patch the resources of a PE image of the packer.
 *
 * The packer decorates the launcher payload before it is written into an
 * archive: the icon of the packaged application and the version information of
 * its main program are both resources of the payload image. The work is the
 * same for both of them - map the image as a data file, read what is needed,
 * copy the payload into a temporary file, update the resources there and read
 * the patched image back - so the pieces are shared here instead of being
 * written twice.
 *
 * Every helper works on a host path or on a byte range only; none of them
 * executes the image it touches. The resource update API of Windows works on
 * files, which is the reason for the temporary copy.
 */
namespace appbox::pe_resource
{

/** Offset of the PE signature inside the DOS header of an image. */
inline constexpr std::size_t kPeSignatureOffset = 0x3C;

/** Number of attempts of the resource update of one image. */
inline constexpr int kUpdateAttempts = 10;

/**
 * @brief Base wait between two attempts of the resource update in milliseconds.
 *
 * A filter driver which scans the freshly written image holds the file for a
 * moment, so a denied update is repeated after a wait which grows with the
 * attempt: a scanner which is still busy with a large image keeps the file
 * longer than the first short wait.
 */
inline constexpr DWORD kUpdateRetryDelayMs = 250;

/**
 * @brief Longest wait between two attempts of the resource update in
 *        milliseconds.
 *
 * The wait grows with the attempt but stays bounded, so an image which is
 * genuinely locked - by another process which holds it, not by a scanner which
 * is merely busy - does not hold a pack run for minutes before the update is
 * given up.
 */
inline constexpr DWORD kUpdateRetryLimitMs = 1000;

/**
 * @brief RAII wrapper of a module handle returned by LoadLibraryExW.
 *
 * The handle of an image which was loaded as a data file has to be released
 * before the file can be opened for writing by the resource update API.
 */
class LoadedImage
{
public:
    /**
     * @brief Take ownership of a module handle.
     * @param[in] module Module handle, may be nullptr.
     */
    explicit LoadedImage(HMODULE module) : module_(module)
    {
    }

    ~LoadedImage()
    {
        if (module_ != nullptr)
        {
            FreeLibrary(module_);
        }
    }

    LoadedImage(const LoadedImage&) = delete;
    LoadedImage& operator=(const LoadedImage&) = delete;

    /**
     * @brief Get the module handle.
     * @return The handle, nullptr when the image was not loaded.
     */
    HMODULE Get() const
    {
        return module_;
    }

    /**
     * @brief Whether the image was loaded.
     * @return true when the handle is valid.
     */
    explicit operator bool() const
    {
        return module_ != nullptr;
    }

private:
    HMODULE module_;
};

/**
 * @brief RAII wrapper of a resource update session.
 *
 * The session is discarded unless Commit() was called, so every error path
 * leaves the file unchanged.
 */
class UpdateSession
{
public:
    /**
     * @brief Take ownership of a resource update handle.
     * @param[in] handle Handle returned by BeginUpdateResourceW.
     */
    explicit UpdateSession(HANDLE handle) : handle_(handle)
    {
    }

    ~UpdateSession()
    {
        if (handle_ != nullptr)
        {
            /* Discard the pending changes of an uncommitted session. */
            EndUpdateResourceW(handle_, TRUE);
        }
    }

    UpdateSession(const UpdateSession&) = delete;
    UpdateSession& operator=(const UpdateSession&) = delete;

    /**
     * @brief Get the update handle.
     * @return The handle.
     */
    HANDLE Get() const
    {
        return handle_;
    }

    /**
     * @brief Write the pending resources to the file.
     * @return true on success.
     */
    bool Commit()
    {
        if (handle_ == nullptr)
        {
            return false;
        }

        const HANDLE handle = handle_;
        handle_ = nullptr;
        return EndUpdateResourceW(handle, FALSE) != FALSE;
    }

private:
    HANDLE handle_;
};

/**
 * @brief RAII wrapper of a temporary file.
 */
class TemporaryFile
{
public:
    /**
     * @brief Remember the path of the file to remove.
     * @param[in] path Path of the temporary file.
     */
    explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path))
    {
    }

    ~TemporaryFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    /**
     * @brief Get the path of the temporary file.
     * @return The path.
     */
    const std::filesystem::path& Path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Whether a buffer starts with a PE image.
 *
 * @param[in] bytes Buffer to inspect.
 * @param[in] size Buffer size in bytes.
 * @return true when the DOS and the PE signature are present.
 */
inline bool LooksLikePeImage(const void* bytes, std::size_t size)
{
    if (bytes == nullptr || size < kPeSignatureOffset + sizeof(std::uint32_t))
    {
        return false;
    }

    const auto* data = static_cast<const unsigned char*>(bytes);
    if (data[0] != 'M' || data[1] != 'Z')
    {
        return false;
    }

    std::uint32_t signature_offset = 0;
    std::memcpy(&signature_offset, data + kPeSignatureOffset, sizeof(signature_offset));
    if (signature_offset + 4 > size)
    {
        return false;
    }

    return data[signature_offset] == 'P' && data[signature_offset + 1] == 'E' && data[signature_offset + 2] == 0 &&
           data[signature_offset + 3] == 0;
}

/**
 * @brief Map an executable as a data file without running it.
 *
 * LOAD_LIBRARY_AS_IMAGE_RESOURCE lets the resource functions return pointers
 * into the mapping; plain data file loading is used as the fallback when a
 * mapping is refused. Both forms work for 32 bit and 64 bit images and never
 * execute the mapped code.
 *
 * @param[in] path Host path of the image.
 * @param[out] error Error description on failure.
 * @return The module handle, nullptr on failure.
 */
inline HMODULE LoadAsDataFile(const std::wstring& path, std::string& error)
{
    const DWORD flags[] = { LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE, LOAD_LIBRARY_AS_DATAFILE };
    for (const auto flag : flags)
    {
        if (HMODULE module = LoadLibraryExW(path.c_str(), nullptr, flag); module != nullptr)
        {
            return module;
        }
    }

    error = "failed to open '" + WideToUTF8(path) + "' as a data file (error " + std::to_string(GetLastError()) + ")";
    return nullptr;
}

/**
 * @brief Write a buffer to a file.
 *
 * @param[in] path Destination path.
 * @param[in] data Buffer to write.
 * @param[in] size Buffer size in bytes.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
inline bool WriteFileBytes(const std::filesystem::path& path, const void* data, std::size_t size, std::string& error)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "failed to create '" + WideToUTF8(path.wstring()) + "'";
        return false;
    }

    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.close();
    if (!file)
    {
        error = "failed to write '" + WideToUTF8(path.wstring()) + "'";
        return false;
    }

    return true;
}

/**
 * @brief Read a whole file into a buffer.
 *
 * @param[in] path Source path.
 * @param[out] bytes The file content.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
inline bool ReadFileBytes(const std::filesystem::path& path, std::vector<char>& bytes, std::string& error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "failed to open '" + WideToUTF8(path.wstring()) + "'";
        return false;
    }

    const auto size = file.tellg();
    if (size < 0)
    {
        error = "failed to measure '" + WideToUTF8(path.wstring()) + "'";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    if (!file)
    {
        error = "failed to read '" + WideToUTF8(path.wstring()) + "'";
        return false;
    }

    return true;
}

/**
 * @brief Build the path of a temporary image of the packer.
 *
 * @param[in] prefix Prefix of the file name, e.g. `L"AppBox-Icon"`.
 * @return A unique path below the temporary directory of the process.
 */
inline std::filesystem::path TemporaryPath(const std::wstring& prefix)
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto name = prefix + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(ticks) + L".exe";
    return std::filesystem::temp_directory_path() / name;
}

/**
 * @brief Wait before the next attempt of a resource update.
 *
 * @param[in] attempt Number of the attempt which was denied, starting at one.
 */
inline void WaitForResourceUpdate(int attempt)
{
    const DWORD delay = kUpdateRetryDelayMs * static_cast<DWORD>(attempt);
    Sleep(delay > kUpdateRetryLimitMs ? kUpdateRetryLimitMs : delay);
}

/**
 * @brief Compare two resource names the way the resource API does.
 *
 * The resource update API stores the names of the resources in upper case and
 * the lookup functions compare them case insensitively, so the comparison
 * follows that rule instead of comparing the strings literally.
 *
 * @param[in] left First resource name.
 * @param[in] right Second resource name.
 * @return true when both names address the same resource.
 */
inline bool SameResourceName(const std::wstring& left, const std::wstring& right)
{
    const auto result = CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                             static_cast<int>(right.size()), TRUE);
    return result == CSTR_EQUAL;
}

} // namespace appbox::pe_resource

#endif // APPBOX_PACKER_CORE_PE_RESOURCE_PATCH_HPP
