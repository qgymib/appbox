#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegDeleteValue.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"

/**
 * @brief Delete a value of the view, read it back and enumerate the values.
 *
 * The delete runs inside the sandbox, so the value of the real registry must
 * survive: a value which only the host holds is recorded as deleted in the
 * sandbox hive instead (a whiteout).
 */
static nlohmann::json ProbeRegDeleteValue_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegDeleteValue::Req>();

    appbox::test::ProtocolRegDeleteValue::Rsp rsp;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.open_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    const auto key_path   = appbox::UTF8ToWide(req.Key);
    const auto value_name = appbox::UTF8ToWide(req.Value);

    HKEY key = nullptr;
    rsp.open_code = RegOpenKeyExW(root, key_path.c_str(), 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (rsp.open_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    rsp.delete_code = RegDeleteValueW(key, value_name.c_str());
    RegCloseKey(key);

    /* What does the view show now? */
    HKEY probe = nullptr;
    if (RegOpenKeyExW(root, key_path.c_str(), 0, KEY_QUERY_VALUE, &probe) != ERROR_SUCCESS)
    {
        return rsp;
    }

    wchar_t buffer[128] = {};
    DWORD   size = sizeof(buffer);
    rsp.query_code = RegQueryValueExW(probe, value_name.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);

    /* The merged value enumeration, so a test can see whether the name is gone. */
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[256] = {};
        DWORD   name_len  = sizeof(name) / sizeof(name[0]);
        const LONG ret    = RegEnumValueW(probe, index, name, &name_len, nullptr, nullptr, nullptr, nullptr);
        if (ret == ERROR_NO_MORE_ITEMS)
        {
            rsp.enum_code = ERROR_SUCCESS;
            break;
        }
        if (ret != ERROR_SUCCESS)
        {
            rsp.enum_code = static_cast<DWORD>(ret);
            break;
        }

        rsp.names.push_back(appbox::WideToUTF8(name));
    }

    RegCloseKey(probe);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegDeleteValue("RegDeleteValue", ProbeRegDeleteValue_Entry);
