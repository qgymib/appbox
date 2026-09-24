#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <vector>
#include "CommandLine.hpp"

std::wstring appbox::test::GetOwnCommandLine()
{
    const wchar_t* command_line = ::GetCommandLineW();
    return command_line != nullptr ? std::wstring(command_line) : std::wstring();
}

bool appbox::test::FindCommandLineOption(const std::wstring& command_line, const std::wstring& name,
                                        std::wstring& value)
{
    value.clear();
    if (name.empty())
    {
        return false;
    }

    const std::wstring prefix = L"--" + name + L"=";
    size_t             pos = command_line.find(prefix);
    while (pos != std::wstring::npos)
    {
        /*
         * The option has to start a token, otherwise a longer option which
         * ends with the searched name would match as well. A token whose value
         * holds a space is quoted as a whole, so the quote in front of the
         * option is the start of a token as well.
         */
        const bool quoted_token = pos > 0 && command_line[pos - 1] == L'"';
        const bool starts_token =
            pos == 0 || quoted_token || command_line[pos - 1] == L' ' || command_line[pos - 1] == L'\t';
        if (!starts_token)
        {
            pos = command_line.find(prefix, pos + 1);
            continue;
        }

        const size_t begin = pos + prefix.size();
        std::wstring found;

        if (quoted_token)
        {
            /* The whole option is quoted, because its value holds a space:
             * everything up to the closing quote is the value. */
            const size_t end = command_line.find(L'"', begin);
            const size_t stop = end != std::wstring::npos ? end : command_line.size();
            found = command_line.substr(begin, stop - begin);
        }
        else if (begin < command_line.size() && command_line[begin] == L'"')
        {
            /* Only the value is quoted: everything up to the closing quote. */
            const size_t end = command_line.find(L'"', begin + 1);
            const size_t stop = end != std::wstring::npos ? end : command_line.size();
            found = command_line.substr(begin + 1, stop - begin - 1);
        }
        else
        {
            const size_t end = command_line.find_first_of(L" \t", begin);
            const size_t stop = end != std::wstring::npos ? end : command_line.size();
            found = command_line.substr(begin, stop - begin);
        }

        value = std::move(found);
        return true;
    }

    return false;
}

bool appbox::test::ReadEnvironmentVariable(const std::wstring& name, std::wstring& value)
{
    value.clear();
    if (name.empty())
    {
        return false;
    }

    /* Query the required buffer size. */
    ::SetLastError(ERROR_SUCCESS);
    const DWORD required = ::GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required == 0)
    {
        /* An existing but empty variable has to be told apart from a missing
         * one. */
        return ::GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    }

    std::vector<wchar_t> buffer(required);
    const DWORD          written = ::GetEnvironmentVariableW(name.c_str(), buffer.data(), required);
    if (written == 0 || written >= required)
    {
        return false;
    }

    value.assign(buffer.data(), written);
    return true;
}
