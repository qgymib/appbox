#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegEnumValue.hpp"
#include "WString.hpp"

/**
 * @brief Open a key below HKCU, enumerate its values and read each one back.
 *
 * The enumeration and the read back run inside the sandbox. Reading every
 * enumerated value verifies that the merged value enumeration and the read
 * through of the value query stay consistent.
 */
static nlohmann::json ProbeRegEnumValue_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegEnumValue::Req>();

    appbox::test::ProtocolRegEnumValue::Rsp rsp;

    HKEY key = nullptr;
    rsp.open_code =
        RegOpenKeyExW(HKEY_CURRENT_USER, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_READ, &key);
    if (rsp.open_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    for (DWORD i = 0;; ++i)
    {
        wchar_t name[16384] = {};
        DWORD   name_len = (DWORD)std::size(name);
        DWORD   type = 0;
        LONG rc = RegEnumValueW(key, i, name, &name_len, nullptr, &type, nullptr, nullptr);
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

        const auto value_name = appbox::WideToUTF8(name);
        rsp.names.push_back(value_name);

        wchar_t buf[128] = {};
        DWORD   buf_size = sizeof(buf);
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &buf_size) == ERROR_SUCCESS)
        {
            rsp.values[value_name] = appbox::WideToUTF8(buf);
        }
    }

    RegCloseKey(key);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegEnumValue("RegEnumValue", ProbeRegEnumValue_Entry);
