#include "WinAPI.h" /* Must be first include file */
#include "RegWriteValue.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"

/**
 * @brief Create a key below a root key, write a REG_SZ value and read it back.
 *
 * All three operations run inside the sandbox, so the closed loop
 * write -> read is verified with the redirection active.
 */
appbox::test::ProtocolRegWriteValue::Rsp appbox::test::ProbeRegWriteValue_Entry(
    const appbox::test::ProtocolRegWriteValue::Req& request)
{
    const auto& req = request;

    appbox::test::ProtocolRegWriteValue::Rsp rsp;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.create_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    HKEY key = nullptr;
    rsp.create_code = RegCreateKeyExW(root, appbox::UTF8ToWide(req.Key).c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                                      &key, &rsp.disposition);
    if (rsp.create_code == ERROR_SUCCESS)
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
