#include "RegistryRootKey.hpp"
#include <cstring>
#include <sddl.h>
#include <vector>

HKEY appbox::test::RegistryRootHandle(const std::string& name)
{
    struct Entry
    {
        const char* name;
        HKEY        key;
    };

    static const Entry entries[] = {
        { "HKEY_CLASSES_ROOT",   HKEY_CLASSES_ROOT   },
        { "HKEY_CURRENT_USER",   HKEY_CURRENT_USER   },
        { "HKEY_LOCAL_MACHINE",  HKEY_LOCAL_MACHINE  },
        { "HKEY_USERS",          HKEY_USERS          },
        { "HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
    };

    if (name.empty())
    {
        return HKEY_CURRENT_USER;
    }

    for (const auto& entry : entries)
    {
        if (_stricmp(name.c_str(), entry.name) == 0)
        {
            return entry.key;
        }
    }

    return nullptr;
}

std::wstring appbox::test::CurrentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return std::wstring();
    }

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);

    std::wstring      sid;
    std::vector<BYTE> buffer(size);
    if (size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
    {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(user->User.Sid, &text))
        {
            sid = text;
            LocalFree(text);
        }
    }

    CloseHandle(token);
    return sid;
}
