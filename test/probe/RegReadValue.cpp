#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegReadValue.hpp"
#include "WString.hpp"

/**
 * @brief Open a key below HKCU and read a REG_SZ value.
 *
 * The read runs inside the sandbox, so the read through fallback has to
 * deliver values which only exist in the real registry.
 */
static nlohmann::json ProbeRegReadValue_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegReadValue::Req>();

    appbox::test::ProtocolRegReadValue::Rsp rsp;

    HKEY key = nullptr;
    rsp.open_code =
        RegOpenKeyExW(HKEY_CURRENT_USER, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_QUERY_VALUE, &key);
    if (rsp.open_code == ERROR_SUCCESS)
    {
        wchar_t buf[128] = {};
        DWORD   buf_size = sizeof(buf);
        rsp.query_code = RegQueryValueExW(key, appbox::UTF8ToWide(req.Value).c_str(), nullptr, &rsp.type,
                                          reinterpret_cast<LPBYTE>(buf), &buf_size);
        if (rsp.query_code == ERROR_SUCCESS)
        {
            rsp.data = appbox::WideToUTF8(buf);
        }

        RegCloseKey(key);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegReadValue("RegReadValue", ProbeRegReadValue_Entry);
