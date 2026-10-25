#include <windows.h>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include "EmbeddedResource.hpp"
#include "EmbeddedResourceIds.h"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "SandboxModules.hpp"

namespace
{

/**
 * @brief Report a failure of the Win32 API as an error message.
 * @param[in] context Description of the failed operation.
 * @param[in] code Status code the operation reported.
 * @return The composed message.
 */
std::string FormatWin32Error(const char* context, DWORD code)
{
    return std::string(context) + " (error " + std::to_string(code) + ": " +
           std::system_category().message(static_cast<int>(code)) + ")";
}

/**
 * @brief Whether a file exists and has exactly the given size.
 *
 * The attributes are read without opening the file for data access, so the
 * check works while another process holds the file without sharing it.
 *
 * @param[in] path Path of the file.
 * @param[in] size Expected size in bytes.
 * @return true when a regular file of that size is there.
 */
bool ExistingFileHasSize(const std::wstring& path, std::size_t size)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
    {
        return false;
    }

    if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }

    ULARGE_INTEGER length = {};
    length.LowPart = attributes.nFileSizeLow;
    length.HighPart = attributes.nFileSizeHigh;
    return length.QuadPart == static_cast<ULONGLONG>(size);
}

/**
 * @brief Write one module into the cache directory.
 *
 * The file is truncated and rewritten on every call. A sharing conflict is the
 * only failure which falls back to the existing file, because it is the normal
 * outcome of a second launcher whose predecessor already loaded the module.
 *
 * @param[in] path Destination path of the module.
 * @param[in] payload Bytes of the module.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the file carries the module.
 */
bool WriteModule(const std::wstring& path, std::string_view payload, std::string& error)
{
    error.clear();

    if (payload.size() > std::numeric_limits<DWORD>::max())
    {
        error = "the sandbox module is too large to be written";
        return false;
    }

    const std::filesystem::path destination(path);
    std::error_code             ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
    {
        error = "failed to create '" + appbox::WideToUTF8(destination.parent_path().wstring()) + "': " + ec.message();
        return false;
    }

    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD last_error = GetLastError();
        if ((last_error == ERROR_SHARING_VIOLATION || last_error == ERROR_LOCK_VIOLATION) &&
            ExistingFileHasSize(path, payload.size()))
        {
            SPDLOG_WARN("the sandbox module '{}' is in use, the existing file is reused", appbox::WideToUTF8(path));
            return true;
        }

        error = FormatWin32Error(("failed to write '" + appbox::WideToUTF8(path) + "'").c_str(), last_error);
        return false;
    }

    const char* data = payload.data();
    DWORD       remaining = static_cast<DWORD>(payload.size());
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(file, data, remaining, &written, nullptr) || written == 0)
        {
            const DWORD last_error = GetLastError();
            CloseHandle(file);
            std::filesystem::remove(destination, ec);
            error = FormatWin32Error(("failed to write '" + appbox::WideToUTF8(path) + "'").c_str(), last_error);
            return false;
        }

        data += written;
        remaining -= written;
    }

    if (!CloseHandle(file))
    {
        const DWORD last_error = GetLastError();
        std::filesystem::remove(destination, ec);
        error = FormatWin32Error(("failed to close '" + appbox::WideToUTF8(path) + "'").c_str(), last_error);
        return false;
    }

    return true;
}

} // namespace

bool appbox::ReleaseSandboxModules(const std::wstring& cache_dir, SandboxModulePaths& modules, std::string& error)
{
    modules = {};
    error.clear();

    std::string_view payload32;
    if (!appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_SANDBOX32, payload32, error))
    {
        error = "failed to read the 32 bit sandbox module: " + error;
        return false;
    }

    std::string_view payload64;
    if (!appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_SANDBOX64, payload64, error))
    {
        error = "failed to read the 64 bit sandbox module: " + error;
        return false;
    }

    const std::filesystem::path directory(cache_dir);
    modules.sandbox32_dll = (directory / appbox::layout::kSandbox32DllNameW).wstring();
    modules.sandbox64_dll = (directory / appbox::layout::kSandbox64DllNameW).wstring();

    if (!WriteModule(modules.sandbox32_dll, payload32, error))
    {
        return false;
    }

    if (!WriteModule(modules.sandbox64_dll, payload64, error))
    {
        return false;
    }

    return true;
}
