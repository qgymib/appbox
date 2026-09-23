#include "RegistryRootKey.hpp"
#include <cstring>

HKEY appbox::test::RegistryRootHandle(const std::string& name)
{
    struct Entry
    {
        const char* name;
        HKEY        key;
    };

    static const Entry entries[] = {
        { "HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT },     { "HKEY_CURRENT_USER", HKEY_CURRENT_USER },
        { "HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE },   { "HKEY_USERS", HKEY_USERS },
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
