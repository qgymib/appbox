#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegQueryKeyName.hpp"
#include "WString.hpp"
#include <vector>

/**
 * @brief Create a key below HKCU and query its kernel object names.
 *
 * The creation is redirected into the sandbox hive, so the key handle points
 * below the private hive mount. The names of NtQueryKey() and NtQueryObject()
 * must address the logical view (\REGISTRY\USER\<SID>\...) instead of the
 * private mount (\REGISTRY\A\{GUID}\...), so the sandboxed process cannot
 * observe the implementation detail of the registry isolation.
 */
static nlohmann::json ProbeRegQueryKeyName_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegQueryKeyName::Req>();

    appbox::test::ProtocolRegQueryKeyName::Rsp rsp;

    HKEY key = nullptr;
    rsp.create_code = RegCreateKeyExW(HKEY_CURRENT_USER, appbox::UTF8ToWide(req.Key).c_str(), 0, nullptr, 0,
                                      KEY_ALL_ACCESS, nullptr, &key, &rsp.disposition);
    if (rsp.create_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        auto fn_query_key = reinterpret_cast<NTSTATUS (*)(HANDLE, ULONG, PVOID, ULONG, PULONG)>(
            GetProcAddress(ntdll, "NtQueryKey"));
        if (fn_query_key != nullptr)
        {
            /* KeyNameInformation == 3 */
            BYTE  buf[1024] = {};
            ULONG needed = 0;
            NTSTATUS st = fn_query_key(key, 3, buf, sizeof(buf), &needed);
            rsp.query_key_code = static_cast<DWORD>(st);
            if (NT_SUCCESS(st))
            {
                /* ULONG NameLength; WCHAR Name[]; */
                ULONG name_len = 0;
                memcpy(&name_len, buf, sizeof(name_len));
                if (name_len > 0 && 4 + name_len <= sizeof(buf))
                {
                    rsp.key_name =
                        appbox::WideToUTF8(std::wstring(reinterpret_cast<const wchar_t*>(buf + 4),
                                                        name_len / sizeof(wchar_t)));
                }
            }
        }

        auto fn_query_object = reinterpret_cast<NTSTATUS (*)(HANDLE, ULONG, PVOID, ULONG, PULONG)>(
            GetProcAddress(ntdll, "NtQueryObject"));
        if (fn_query_object != nullptr)
        {
            /* ObjectNameInformation == 1 */
            BYTE  buf[1024] = {};
            ULONG needed = 0;
            NTSTATUS st = fn_query_object(key, 1, buf, sizeof(buf), &needed);
            rsp.query_object_code = static_cast<DWORD>(st);
            if (NT_SUCCESS(st))
            {
                /* UNICODE_STRING Name; */
                USHORT length = 0;
                memcpy(&length, buf, sizeof(length));
                void* buffer = nullptr;
                memcpy(&buffer, reinterpret_cast<BYTE*>(buf) + sizeof(void*), sizeof(buffer));
                if (length > 0 && buffer != nullptr)
                {
                    rsp.object_name =
                        appbox::WideToUTF8(std::wstring(reinterpret_cast<const wchar_t*>(buffer),
                                                        length / sizeof(wchar_t)));
                }
            }
        }
    }

    RegCloseKey(key);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegQueryKeyName("RegQueryKeyName", ProbeRegQueryKeyName_Entry);
