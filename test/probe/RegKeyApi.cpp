#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegKeyApi.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Signature of the information setter of ntdll.
 */
typedef NTSTATUS (*T_NtSetInformationKey)(HANDLE KeyHandle, KEY_SET_INFORMATION_CLASS KeySetInformationClass,
                                          PVOID KeySetInformation, ULONG KeySetInformationLength);

/**
 * @brief Signature of the calls which act on the key or on its hive file.
 */
typedef NTSTATUS (*T_NtKeyHandleCall)(HANDLE KeyHandle);

/**
 * @brief Last write time of a key as the sandboxed process sees it.
 *
 * @param[in] key The key handle.
 * @param[out] code Error code of the query.
 * @return The time in FILETIME ticks, zero when the query failed.
 */
uint64_t ReadWriteTime(HKEY key, DWORD& code)
{
    FILETIME time = {};
    code = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                            nullptr, &time);
    if (code != ERROR_SUCCESS)
    {
        return 0;
    }

    return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

/**
 * @brief Name of the entry point the request selects, null for an unknown one.
 */
const char* ApiNameOf(const std::string& api)
{
    if (api == "NtFlushKey")
    {
        return "NtFlushKey";
    }
    if (api == "NtCompressKey")
    {
        return "NtCompressKey";
    }
    if (api == "NtLockRegistryKey")
    {
        return "NtLockRegistryKey";
    }
    return nullptr;
}

} // namespace

/**
 * @brief Call one of the remaining key APIs on a handle of a selected layer.
 *
 * The mode decides the layer of the handle: a write access open of a key the
 * hive does not hold is copied up into the hive, a read access open of such a
 * key is answered by the real registry (the read through). The call itself is
 * one of the entry points the registry isolation hooks, so a handle of the host
 * layer must not let the call act on the real registry, while the hive is the
 * write layer of the sandbox and its handle is forwarded.
 */
static nlohmann::json ProbeRegKeyApi_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegKeyApi::Req>();

    appbox::test::ProtocolRegKeyApi::Rsp rsp;

    const auto   key_path = appbox::UTF8ToWide(req.Key);
    const REGSAM access = req.Mode == "hive_handle" ? (KEY_SET_VALUE | KEY_QUERY_VALUE) : KEY_READ;

    HKEY key = nullptr;
    rsp.open_code = RegOpenKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, access, &key);
    if (rsp.open_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    DWORD ignored = 0;
    rsp.write_time_before = ReadWriteTime(key, ignored);

    auto* ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        if (req.Api == "NtSetInformationKey")
        {
            auto* fn = reinterpret_cast<T_NtSetInformationKey>(GetProcAddress(ntdll, "NtSetInformationKey"));
            if (fn != nullptr)
            {
                KEY_WRITE_TIME_INFORMATION info = {};
                info.LastWriteTime.QuadPart = static_cast<LONGLONG>(appbox::test::kProbeWriteTime);
                rsp.api_code =
                    static_cast<DWORD>(fn(reinterpret_cast<HANDLE>(key), KeyWriteTimeInformation, &info, sizeof(info)));
            }
        }
        else if (const char* name = ApiNameOf(req.Api); name != nullptr)
        {
            auto* fn = reinterpret_cast<T_NtKeyHandleCall>(GetProcAddress(ntdll, name));
            if (fn != nullptr)
            {
                rsp.api_code = static_cast<DWORD>(fn(reinterpret_cast<HANDLE>(key)));
            }
        }
    }

    rsp.write_time_after = ReadWriteTime(key, rsp.query_code);

    RegCloseKey(key);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegKeyApi("RegKeyApi", ProbeRegKeyApi_Entry);
