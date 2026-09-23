#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegDeleteKey.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Signature of the delete entry point of ntdll.
 */
typedef NTSTATUS (*T_NtDeleteKey)(HANDLE KeyHandle);

} // namespace

/**
 * @brief Delete a key of the view and report what the view shows afterwards.
 *
 * The delete runs inside the sandbox, so the registry isolation has to keep
 * the key of the real registry untouched: a key which only the host holds is
 * recorded as deleted in the sandbox hive instead (a whiteout), and a delete
 * which runs on a read through handle of the host is refused.
 */
static nlohmann::json ProbeRegDeleteKey_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegDeleteKey::Req>();

    appbox::test::ProtocolRegDeleteKey::Rsp rsp;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.open_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    const auto key_path = appbox::UTF8ToWide(req.Key);

    if (req.Mode == "read_handle")
    {
        /*
         * A read access open is answered by the real registry (the read
         * through), so the handle is not a handle of the hive.
         */
        HKEY key = nullptr;
        rsp.open_code = RegOpenKeyExW(root, key_path.c_str(), 0, KEY_READ, &key);
        if (rsp.open_code == ERROR_SUCCESS)
        {
            auto* fn = reinterpret_cast<T_NtDeleteKey>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtDeleteKey"));
            if (fn != nullptr)
            {
                rsp.delete_code = static_cast<DWORD>(fn(reinterpret_cast<HANDLE>(key)));
            }
            RegCloseKey(key);
        }
    }
    else
    {
        rsp.delete_code = RegDeleteKeyW(root, key_path.c_str());
    }

    /* What does the view show now? */
    HKEY probe = nullptr;
    rsp.reopen_code = RegOpenKeyExW(root, key_path.c_str(), 0, KEY_READ, &probe);
    if (rsp.reopen_code == ERROR_SUCCESS)
    {
        wchar_t buffer[128] = {};
        DWORD   size = sizeof(buffer);
        rsp.query_code =
            RegQueryValueExW(probe, L"HostValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);
        if (rsp.query_code == ERROR_SUCCESS)
        {
            rsp.readback = appbox::WideToUTF8(buffer);
        }

        RegCloseKey(probe);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegDeleteKey("RegDeleteKey", ProbeRegDeleteKey_Entry);
