#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegOpenWriteValue.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"

/**
 * @brief Open an existing key with write access, write a REG_SZ value and read it back.
 *
 * The write access open runs inside the sandbox, so the registry isolation has
 * to keep the modification out of the host registry: a key which the sandbox
 * hive does not hold is copied up into the hive instead of being handed out as
 * a real key handle.
 */
static nlohmann::json ProbeRegOpenWriteValue_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegOpenWriteValue::Req>();

    appbox::test::ProtocolRegOpenWriteValue::Rsp rsp;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.open_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    HKEY key = nullptr;
    rsp.open_code = RegOpenKeyExW(root, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (rsp.open_code == ERROR_SUCCESS)
    {
        auto wdata = appbox::UTF8ToWide(req.Data);
        rsp.set_code = RegSetValueExW(key, appbox::UTF8ToWide(req.Value).c_str(), 0, REG_SZ,
                                      reinterpret_cast<const BYTE*>(wdata.c_str()),
                                      static_cast<DWORD>((wdata.size() + 1) * sizeof(wchar_t)));

        wchar_t buf[128] = {};
        DWORD   buf_size = sizeof(buf);
        rsp.query_code = RegQueryValueExW(key, appbox::UTF8ToWide(req.Value).c_str(), nullptr, &rsp.type,
                                          reinterpret_cast<LPBYTE>(buf), &buf_size);
        if (rsp.query_code == ERROR_SUCCESS)
        {
            rsp.readback = appbox::WideToUTF8(buf);
        }

        RegCloseKey(key);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegOpenWriteValue("RegOpenWriteValue", ProbeRegOpenWriteValue_Entry);
