#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegEnumKey.hpp"
#include "WString.hpp"

/**
 * @brief Open a key below HKCU and enumerate its sub keys.
 *
 * The enumeration runs inside the sandbox, so the merged two layer view of the
 * registry isolation has to deliver the sub keys of the hive layer and of the
 * real registry.
 */
static nlohmann::json ProbeRegEnumKey_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegEnumKey::Req>();

    appbox::test::ProtocolRegEnumKey::Rsp rsp;

    HKEY key = nullptr;
    rsp.open_code =
        RegOpenKeyExW(HKEY_CURRENT_USER, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_READ, &key);
    if (rsp.open_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    rsp.count_code = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &rsp.subkey_count, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, nullptr, nullptr);

    for (DWORD i = 0;; ++i)
    {
        wchar_t name[256] = {};
        DWORD   name_len = (DWORD)std::size(name);
        LONG rc = RegEnumKeyExW(key, i, name, &name_len, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS)
        {
            rsp.enum_code = 0;
            break;
        }
        if (rc != ERROR_SUCCESS)
        {
            rsp.enum_code = static_cast<DWORD>(rc);
            break;
        }
        rsp.names.push_back(appbox::WideToUTF8(name));
    }

    RegCloseKey(key);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegEnumKey("RegEnumKey", ProbeRegEnumKey_Entry);
