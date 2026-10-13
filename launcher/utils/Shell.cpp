#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "StartupFileType.hpp"
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

std::wstring appbox::ResolvePowerShellPath(const std::wstring& system_root)
{
    if (system_root.empty())
    {
        return std::wstring();
    }

    std::wstring path = system_root;
    if (path.back() != L'\\' && path.back() != L'/')
    {
        path.push_back(L'\\');
    }
    path += L"System32\\WindowsPowerShell\\v1.0\\powershell.exe";

    if (IsExistingFile(path))
    {
        return path;
    }

    return std::wstring();
}

std::wstring appbox::ResolvePowerShellPath()
{
    return ResolvePowerShellPath(ReadEnvironmentVariable(L"SystemRoot"));
}

bool appbox::BuildStartupInvocation(const std::wstring& path, const std::vector<std::wstring>& args,
                                    std::wstring& exe_path, std::vector<std::wstring>& exe_args, std::string& error)
{
    exe_path.clear();
    exe_args.clear();
    error.clear();

    switch (appbox::StartupFileKindOf(path))
    {
    case StartupFileKind::Program:
        /* A program image is created as the process itself. */
        exe_path = path;
        exe_args = args;
        return true;

    case StartupFileKind::Batch:
    case StartupFileKind::CommandScript: {
        exe_path = ResolveShellPath();
        if (exe_path.empty())
        {
            error = "the command interpreter of the host was not found for '" + appbox::WideToUTF8(path) + "'";
            return false;
        }

        /*
         * The interpreter runs the script and leaves: the script is the command
         * of `/c`, so the arguments of the script follow it.
         */
        exe_args.reserve(args.size() + 2);
        exe_args.push_back(L"/c");
        exe_args.push_back(path);
        exe_args.insert(exe_args.end(), args.begin(), args.end());
        return true;
    }

    case StartupFileKind::PowerShellScript: {
        exe_path = ResolvePowerShellPath();
        if (exe_path.empty())
        {
            error = "Windows PowerShell was not found for '" + appbox::WideToUTF8(path) + "'";
            return false;
        }

        /*
         * The execution policy of a machine refuses a script by default, so the
         * run bypasses it; the profile is left out to keep the run the same on
         * every machine.
         */
        exe_args.reserve(args.size() + 5);
        exe_args.push_back(L"-NoProfile");
        exe_args.push_back(L"-ExecutionPolicy");
        exe_args.push_back(L"Bypass");
        exe_args.push_back(L"-File");
        exe_args.push_back(path);
        exe_args.insert(exe_args.end(), args.begin(), args.end());
        return true;
    }

    case StartupFileKind::None:
    default:
        error =
            "'" + appbox::WideToUTF8(path) + "' is not a startable file (" + appbox::StartupFileExtensionList() + ")";
        return false;
    }
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
