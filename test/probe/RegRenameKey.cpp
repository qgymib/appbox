#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegRenameKey.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Signature of the rename entry point of ntdll.
 */
typedef NTSTATUS (*T_NtRenameKey)(HANDLE KeyHandle, PUNICODE_STRING NewName);

/**
 * @brief Path of the parent key of a key path relative to a root key.
 * @param[in] key_path The key path, for example `Software\AppBoxTest\Case`.
 * @return The path of the parent, empty when the key path has no separator.
 */
std::wstring ParentPath(const std::wstring& key_path)
{
    const auto separator = key_path.find_last_of(L'\\');
    if (separator == std::wstring::npos)
    {
        return {};
    }
    return key_path.substr(0, separator);
}

/**
 * @brief Collect the string values of a key as the view shows them.
 *
 * Only the string types of the registry are reported: they are the values a
 * test uses to tell the layer of an entry apart.
 *
 * @param[in] root The root key handle.
 * @param[in] key_path The path of the key relative to the root key.
 * @param[out] code Error code of the open of the key.
 * @param[out] values The value names and their data.
 */
void DumpStringValues(HKEY root, const std::wstring& key_path, DWORD& code, std::map<std::string, std::string>& values)
{
    HKEY key = nullptr;
    code = RegOpenKeyExW(root, key_path.c_str(), 0, KEY_READ, &key);
    if (code != ERROR_SUCCESS)
    {
        return;
    }

    for (DWORD index = 0;; ++index)
    {
        wchar_t name[512] = {};
        DWORD   name_size = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
        DWORD   type = REG_NONE;
        BYTE    data[512] = {};
        DWORD   data_size = sizeof(data);

        const LONG status = RegEnumValueW(key, index, name, &name_size, nullptr, &type, data, &data_size);
        if (status == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (status != ERROR_SUCCESS)
        {
            break;
        }

        if (type != REG_SZ && type != REG_EXPAND_SZ)
        {
            continue;
        }

        values[appbox::WideToUTF8(name)] = appbox::WideToUTF8(reinterpret_cast<const wchar_t*>(data));
    }

    RegCloseKey(key);
}

/**
 * @brief Collect the sub key names a key of the view reports.
 * @param[in] root The root key handle.
 * @param[in] key_path The path of the key relative to the root key.
 * @param[out] names The sub key names in enumeration order. Encoding in UTF-8.
 */
void DumpSubKeyNames(HKEY root, const std::wstring& key_path, std::vector<std::string>& names)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, key_path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return;
    }

    for (DWORD index = 0;; ++index)
    {
        wchar_t name[512] = {};
        DWORD   name_size = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));

        if (RegEnumKeyExW(key, index, name, &name_size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        {
            break;
        }

        names.push_back(appbox::WideToUTF8(name));
    }

    RegCloseKey(key);
}

} // namespace

/**
 * @brief Rename a key of the view and report what the view shows afterwards.
 *
 * The rename runs inside the sandbox, so the registry isolation has to keep the
 * real registry untouched: a rename of a shadow key must not leave the host key
 * of the old name visible next to the renamed key of the hive, and a rename
 * through a read through handle of the host layer must be refused.
 */
static nlohmann::json ProbeRegRenameKey_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegRenameKey::Req>();

    appbox::test::ProtocolRegRenameKey::Rsp rsp;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.open_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    const auto key_path = appbox::UTF8ToWide(req.Key);
    const auto new_name = appbox::UTF8ToWide(req.NewName);

    /*
     * The kernel asks a rename for the whole `KEY_WRITE` mask of the handle,
     * so the write access of the hive mode is the one a caller needs for a
     * rename.
     */
    const REGSAM access = req.Mode == "hive_handle" ? (KEY_WRITE | KEY_QUERY_VALUE) : KEY_READ;

    HKEY key = nullptr;
    rsp.open_code = RegOpenKeyExW(root, key_path.c_str(), 0, access, &key);
    if (rsp.open_code == ERROR_SUCCESS)
    {
        auto* ntdll = GetModuleHandleW(L"ntdll.dll");
        auto* fn = ntdll != nullptr ? reinterpret_cast<T_NtRenameKey>(GetProcAddress(ntdll, "NtRenameKey")) : nullptr;
        if (fn != nullptr)
        {
            UNICODE_STRING name;
            name.Buffer = const_cast<PWSTR>(new_name.c_str());
            name.Length = static_cast<USHORT>(new_name.size() * sizeof(wchar_t));
            name.MaximumLength = static_cast<USHORT>(name.Length + sizeof(wchar_t));

            rsp.rename_code = static_cast<DWORD>(fn(reinterpret_cast<HANDLE>(key), &name));
        }

        RegCloseKey(key);
    }

    /* What does the view show now? */
    const auto new_path = key_path.empty() ? new_name : ParentPath(key_path) + L"\\" + new_name;

    DumpStringValues(root, key_path, rsp.old_open_code, rsp.old_values);
    DumpStringValues(root, new_path, rsp.new_open_code, rsp.new_values);
    DumpSubKeyNames(root, ParentPath(key_path), rsp.parent_subkeys);

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegRenameKey("RegRenameKey", ProbeRegRenameKey_Entry);
