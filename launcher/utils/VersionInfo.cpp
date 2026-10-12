#include "VersionInfo.hpp"
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

/**
 * @brief Language and code page of one string table of a version resource.
 */
struct Translation
{
    WORD language;  ///< Language identifier of the table.
    WORD code_page; ///< Code page of the strings of the table.
};

/**
 * @brief Read one string field of the version resource of a file.
 *
 * The resource is queried through the version API of Windows, so the file is
 * never executed and no part of the image is parsed by hand.
 *
 * @param[in] path Path of the file.
 * @param[in] field Name of the field, for example `ProductName`.
 * @return The text of the field, empty when the file does not carry it.
 */
std::wstring ReadVersionString(const std::wstring& path, const wchar_t* field)
{
    DWORD       handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
    {
        return {};
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
    {
        return {};
    }

    /*
     * The string tables of the resource are named after the language and the
     * code page of their strings, which the `Translation` entry lists. A
     * resource may carry several of them, so every table is asked until one of
     * them holds the field.
     */
    Translation* translations = nullptr;
    UINT         translations_size = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
                        &translations_size) ||
        translations == nullptr || translations_size < sizeof(Translation))
    {
        return {};
    }

    const UINT count = translations_size / sizeof(Translation);
    for (UINT index = 0; index < count; ++index)
    {
        wchar_t query[128] = {};
        if (swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", translations[index].language,
                       translations[index].code_page, field) < 0)
        {
            continue;
        }

        wchar_t* value = nullptr;
        UINT     value_size = 0;
        if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &value_size) || value == nullptr ||
            value_size <= 1)
        {
            continue;
        }

        /* The size covers the terminating null character of the string. */
        return std::wstring(value, value_size - 1);
    }

    return {};
}

/**
 * @brief Get the path of the running program.
 * @return The path, empty when it cannot be read.
 */
std::wstring OwnPath()
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');

    while (buffer.size() <= 32768)
    {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }

        if (length < buffer.size())
        {
            return std::wstring(buffer.data(), length);
        }

        /* The name did not fit into the buffer, so it is read with more room. */
        buffer.resize(buffer.size() * 2);
    }

    return {};
}

} // namespace

namespace appbox
{

std::wstring ReadImageDisplayName(const std::wstring& path)
{
    if (path.empty())
    {
        return {};
    }

    const wchar_t* const fields[] = { L"ProductName", L"FileDescription" };
    for (const auto* field : fields)
    {
        const auto value = ReadVersionString(path, field);
        if (!value.empty())
        {
            return value;
        }
    }

    /* A program without a version resource still has a name to show. */
    return std::filesystem::path(path).stem().wstring();
}

std::wstring ReadOwnDisplayName()
{
    return ReadImageDisplayName(OwnPath());
}

} // namespace appbox
