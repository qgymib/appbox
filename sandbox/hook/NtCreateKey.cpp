#include "utils/Log.hpp"
#include "utils/BitParser.hpp"
#include "NtCreateKey.hpp"

T_NtCreateKey sys_NtCreateKey = nullptr;

static appbox::BitData s_DesiredAccess[] = {
    { "KEY_ALL_ACCESS",         KEY_ALL_ACCESS         },
    { "KEY_EXECUTE",            KEY_EXECUTE            },
    { "KEY_WRITE",              KEY_WRITE              },
    { "KEY_READ",               KEY_READ               },
    { "KEY_QUERY_VALUE",        KEY_QUERY_VALUE        },
    { "KEY_SET_VALUE",          KEY_SET_VALUE          },
    { "KEY_CREATE_SUB_KEY",     KEY_CREATE_SUB_KEY     },
    { "KEY_ENUMERATE_SUB_KEYS", KEY_ENUMERATE_SUB_KEYS },
    { "KEY_CREATE_LINK",        KEY_CREATE_LINK        },
    { "KEY_NOTIFY",             KEY_NOTIFY             },
};

static appbox::BitData s_CreateOptions[] = {
    { "REG_OPTION_VOLATILE",       REG_OPTION_VOLATILE       },
    { "REG_OPTION_CREATE_LINK",    REG_OPTION_CREATE_LINK    },
    { "REG_OPTION_BACKUP_RESTORE", REG_OPTION_BACKUP_RESTORE },
    { "REG_OPTION_NON_VOLATILE",   REG_OPTION_NON_VOLATILE   },
};

static nlohmann::json NtCreateKeyLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                          POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex, PUNICODE_STRING Class,
                                          ULONG CreateOptions, PULONG Disposition)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["TitleIndex"] = TitleIndex;
    param["Class"] = appbox::ToJson(Class);
    param["CreateOptions"] = appbox::RegistryKeyCreateOptionsToJson(CreateOptions);
    param["Disposition"] = appbox::PointerToString(Disposition);
    return param;
}

static appbox::LoggerF logger("NtCreateKey", NtCreateKeyLogParam);

static NTSTATUS Hook_NtCreateKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, Disposition);
    return sys_NtCreateKey(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, Disposition);
}

static void LoadNtCreateKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtCreateKey");
    sys_NtCreateKey = reinterpret_cast<T_NtCreateKey>(addr);
}

appbox::HookRecord appbox::HookNtCreateKey = {
    "NtCreateKey",
    LoadNtCreateKey,
    (void**)&sys_NtCreateKey,
    Hook_NtCreateKey,
};

nlohmann::json appbox::RegistryKeyDesiredAccessToJson(ACCESS_MASK DesiredAccess)
{
    return appbox::ParseBit(DesiredAccess, s_DesiredAccess, std::size(s_DesiredAccess));
}

nlohmann::json appbox::RegistryKeyCreateOptionsToJson(ULONG CreateOptions)
{
    return appbox::ParseBit(CreateOptions, s_CreateOptions, std::size(s_CreateOptions));
}
