#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "WString.hpp"
#include "Shell.hpp"

namespace
{

/**
 * @brief Read one environment variable of this process.
 *
 * A missing variable and an empty one both report an empty value: neither of
 * them names a usable shell, so the resolver treats them the same way.
 *
 * @param[in] name Name of the variable.
 * @return Value of the variable, empty when it is not set or empty.
 */
std::wstring ReadEnvironmentVariable(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
    {
        return std::wstring();
    }

    std::wstring value(required, L'\0');
    const DWORD  written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required)
    {
        return std::wstring();
    }

    value.resize(written);
    return value;
}

/**
 * @brief Tell whether a path names an existing file.
 *
 * A directory is refused, because the target of the sandbox has to be an
 * executable: a `%COMSPEC%` which was pointed at a folder would otherwise be
 * handed to the process creation of the sandbox and fail there.
 *
 * @param[in] path Path to test.
 * @return true when the path names an existing file.
 */
bool IsExistingFile(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }

    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }

    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

} // namespace

std::wstring appbox::ResolveShellPath(const std::wstring& comspec, const std::wstring& system_root)
{
    if (IsExistingFile(comspec))
    {
        return comspec;
    }

    if (!system_root.empty())
    {
        std::wstring fallback = system_root;
        if (fallback.back() != L'\\' && fallback.back() != L'/')
        {
            fallback.push_back(L'\\');
        }
        fallback += L"System32\\cmd.exe";

        if (IsExistingFile(fallback))
        {
            return fallback;
        }
    }

    return std::wstring();
}

std::wstring appbox::ResolveShellPath()
{
    return ResolveShellPath(ReadEnvironmentVariable(L"COMSPEC"), ReadEnvironmentVariable(L"SystemRoot"));
}

std::vector<std::wstring> appbox::BuildShellArguments(const std::vector<std::string>& params)
{
    std::vector<std::wstring> args;

    if (params.empty())
    {
        /* Without a command the shell of the host runs interactively. */
        return args;
    }

    args.reserve(params.size() + 1);
    args.push_back(L"/c");
    for (const auto& param : params)
    {
        args.push_back(appbox::UTF8ToWide(param));
    }

    return args;
}
