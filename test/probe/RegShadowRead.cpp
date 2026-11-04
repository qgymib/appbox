#include "WinAPI.h" /* Must be first include file */
#include "RegShadowRead.hpp"
#include "WString.hpp"

/**
 * @brief Create a key inside the sandbox and read a real value through it.
 *
 * The creation lands in the sandbox hive even when the real registry holds a
 * key with the same name, so the key handle is a shadow. Reading the value
 * through the shadow exercises the read through of NtQueryValueKey.
 */
appbox::test::ProtocolRegShadowRead::Rsp appbox::test::ProbeRegShadowRead_Entry(
    const appbox::test::ProtocolRegShadowRead::Req& request)
{
    const auto& req = request;

    appbox::test::ProtocolRegShadowRead::Rsp rsp;

    HKEY               key = nullptr;
    const std::wstring wide_key = appbox::UTF8ToWide(req.Key).value_or(std::wstring());
    const std::wstring wide_value = appbox::UTF8ToWide(req.Value).value_or(std::wstring());

    rsp.create_code = RegCreateKeyExW(HKEY_CURRENT_USER, wide_key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key,
                                      &rsp.disposition);
    if (rsp.create_code == ERROR_SUCCESS)
    {
        wchar_t buf[128] = {};
        DWORD   buf_size = sizeof(buf);
        rsp.query_code =
            RegQueryValueExW(key, wide_value.c_str(), nullptr, &rsp.type, reinterpret_cast<LPBYTE>(buf), &buf_size);
        if (rsp.query_code == ERROR_SUCCESS)
        {
            rsp.data = appbox::WideToUTF8(buf).value_or(std::string());
        }

        RegCloseKey(key);
    }

    return rsp;
}
