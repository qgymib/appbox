#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegTransacted.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"
#include <string>

namespace
{

/**
 * @brief Signature of the transacted key entry points of ntdll.
 * @{
 */
typedef NTSTATUS (*T_NtCreateKeyTransacted)(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                            POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
                                            PUNICODE_STRING Class, ULONG CreateOptions, HANDLE TransactionHandle,
                                            PULONG Disposition);

typedef NTSTATUS (*T_NtOpenKeyTransacted)(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                          POBJECT_ATTRIBUTES ObjectAttributes, HANDLE TransactionHandle);

typedef NTSTATUS (*T_NtOpenKeyTransactedEx)(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                            POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions,
                                            HANDLE TransactionHandle);
/** @} */

/**
 * @brief Signature of the transaction API of ktmw32.
 * @{
 */
typedef HANDLE(WINAPI* T_CreateTransaction)(LPSECURITY_ATTRIBUTES lpTransactionAttributes, LPGUID UOW,
                                            DWORD CreateOptions, DWORD IsolationLevel, DWORD IsolationFlags,
                                            DWORD Timeout, LPWSTR Description);
typedef BOOL(WINAPI* T_CommitTransaction)(HANDLE TransactionHandle);
typedef BOOL(WINAPI* T_RollbackTransaction)(HANDLE TransactionHandle);
/** @} */

typedef VOID(NTAPI* T_RtlInitUnicodeString)(PUNICODE_STRING DestinationString, PCWSTR SourceString);

/**
 * @brief The entry points the probe needs.
 */
struct TransactedApi
{
    T_NtCreateKeyTransacted create_transacted = nullptr;  /* NtCreateKeyTransacted() */
    T_NtOpenKeyTransacted   open_transacted = nullptr;    /* NtOpenKeyTransacted() */
    T_NtOpenKeyTransactedEx open_transacted_ex = nullptr; /* NtOpenKeyTransactedEx() */
    T_CreateTransaction     create_transaction = nullptr; /* CreateTransaction() */
    T_CommitTransaction     commit_transaction = nullptr; /* CommitTransaction() */
    T_RollbackTransaction   rollback_transaction = nullptr;
    T_RtlInitUnicodeString  init_unicode_string = nullptr;
};

/**
 * @brief Resolve the entry points of the probe.
 *
 * @param[out] api The entry points to fill.
 * @return true when every entry point was resolved.
 */
bool ResolveTransactedApi(TransactedApi& api)
{
    auto* ntdll = GetModuleHandleW(L"ntdll.dll");
    auto* ktmw32 = LoadLibraryW(L"ktmw32.dll");
    if (ntdll == nullptr || ktmw32 == nullptr)
    {
        return false;
    }

    api.create_transacted = reinterpret_cast<T_NtCreateKeyTransacted>(GetProcAddress(ntdll, "NtCreateKeyTransacted"));
    api.open_transacted = reinterpret_cast<T_NtOpenKeyTransacted>(GetProcAddress(ntdll, "NtOpenKeyTransacted"));
    api.open_transacted_ex = reinterpret_cast<T_NtOpenKeyTransactedEx>(GetProcAddress(ntdll, "NtOpenKeyTransactedEx"));
    api.create_transaction = reinterpret_cast<T_CreateTransaction>(GetProcAddress(ktmw32, "CreateTransaction"));
    api.commit_transaction = reinterpret_cast<T_CommitTransaction>(GetProcAddress(ktmw32, "CommitTransaction"));
    api.rollback_transaction = reinterpret_cast<T_RollbackTransaction>(GetProcAddress(ktmw32, "RollbackTransaction"));
    api.init_unicode_string = reinterpret_cast<T_RtlInitUnicodeString>(GetProcAddress(ntdll, "RtlInitUnicodeString"));

    return api.create_transacted != nullptr && api.open_transacted != nullptr && api.open_transacted_ex != nullptr &&
           api.create_transaction != nullptr && api.commit_transaction != nullptr &&
           api.rollback_transaction != nullptr && api.init_unicode_string != nullptr;
}

/**
 * @brief Read a `REG_SZ` value through a key handle.
 *
 * @param[in] key The key handle.
 * @param[in] name Name of the value.
 * @param[out] text The text of the value.
 * @return The error code of the read.
 */
DWORD ReadString(HKEY key, const std::wstring& name, std::string& text)
{
    wchar_t buffer[1024] = {};
    DWORD   size = sizeof(buffer);

    const DWORD code = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);
    if (code == ERROR_SUCCESS)
    {
        text = appbox::WideToUTF8(buffer);
    }
    return code;
}

/**
 * @brief Write a `REG_SZ` value through a key handle.
 *
 * @param[in] key The key handle.
 * @param[in] name Name of the value.
 * @param[in] text The text of the value.
 * @return The error code of the write.
 */
DWORD WriteString(HKEY key, const std::wstring& name, const std::wstring& text)
{
    return RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                          static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
}

/**
 * @brief Build the full NT path of a key of the view.
 *
 * The prefixes are the ones the isolation redirects, see the scope table of
 * `docs/RegistryIsolation.md`: the current user root is named after the SID of
 * the sandboxed user, which the probe reads from its own token.
 *
 * @param[in] root Name of the root key, empty for `HKEY_CURRENT_USER`.
 * @param[in] key Path of the key relative to the root key.
 * @param[out] path The NT path of the key.
 * @return true when the root key is one the isolation redirects.
 */
bool BuildViewPath(const std::string& root, const std::wstring& key, std::wstring& path)
{
    const std::wstring suffix = key.empty() ? std::wstring() : L"\\" + key;

    if (root.empty() || root == "HKEY_CURRENT_USER")
    {
        const auto sid = appbox::test::CurrentUserSid();
        if (sid.empty())
        {
            return false;
        }

        path = L"\\REGISTRY\\USER\\" + sid + suffix;
        return true;
    }

    if (root == "HKEY_USERS")
    {
        path = L"\\REGISTRY\\USER" + suffix;
        return true;
    }

    if (root == "HKEY_LOCAL_MACHINE")
    {
        path = L"\\REGISTRY\\MACHINE" + suffix;
        return true;
    }

    if (root == "HKEY_CLASSES_ROOT")
    {
        path = L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES" + suffix;
        return true;
    }

    if (root == "HKEY_CURRENT_CONFIG")
    {
        path = L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\HARDWARE PROFILES\\CURRENT" + suffix;
        return true;
    }

    return false;
}

} // namespace

/**
 * @brief Run a transacted registry call inside the sandbox and report the view around it.
 *
 * The probe owns the transaction: it creates it, runs the transacted entry
 * point, writes and reads a value through the handle the call returned, ends
 * the transaction and reads the key again afterwards. Every step is reported,
 * so a test can pin the transaction of the caller and the isolation at once.
 */
static nlohmann::json ProbeRegTransacted_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegTransacted::Req>();

    appbox::test::ProtocolRegTransacted::Rsp rsp;

    TransactedApi api;
    if (!ResolveTransactedApi(api))
    {
        return rsp;
    }

    /*
     * The object of the call: a key of the view, which the probe addresses with
     * its full NT path because the NT entry points do not accept the predefined
     * handles of the Win32 API, or an application hive the probe mounts itself.
     * The mount is a path outside the view, which is what the case of a
     * forwarded call needs.
     */
    HKEY         mount_root = nullptr; /* The root of a mounted hive, null otherwise. */
    HKEY         view_root = nullptr;  /* The root the probe reads the key through afterwards. */
    std::wstring object_name;

    const auto hive = appbox::UTF8ToWide(req.Hive);
    if (!hive.empty())
    {
        rsp.mount_code = RegLoadAppKeyW(hive.c_str(), &mount_root, KEY_ALL_ACCESS, 0, 0);
        if (rsp.mount_code != ERROR_SUCCESS)
        {
            return rsp;
        }

        view_root = mount_root;
        object_name = appbox::UTF8ToWide(req.Key);
    }
    else
    {
        view_root = appbox::test::RegistryRootHandle(req.Root);
        if (view_root == nullptr || !BuildViewPath(req.Root, appbox::UTF8ToWide(req.Key), object_name))
        {
            rsp.mount_code = ERROR_INVALID_PARAMETER;
            return rsp;
        }

        rsp.mount_code = ERROR_SUCCESS;
    }

    const auto key_path = appbox::UTF8ToWide(req.Key);
    const auto value_name = appbox::UTF8ToWide(req.Value);

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    api.init_unicode_string(&us, object_name.c_str());
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, mount_root, nullptr);

    HANDLE transaction = api.create_transaction(nullptr, nullptr, 0, 0, 0, 0, nullptr);
    if (transaction == nullptr)
    {
        rsp.tx_code = GetLastError();
        if (mount_root != nullptr)
        {
            RegCloseKey(mount_root);
        }
        return rsp;
    }
    rsp.tx_code = ERROR_SUCCESS;

    const ACCESS_MASK access = req.Access == "read" ? KEY_READ : KEY_ALL_ACCESS;

    HANDLE key = nullptr;
    if (req.Api == "open_ex")
    {
        rsp.call_code = static_cast<DWORD>(api.open_transacted_ex(&key, access, &oa, 0, transaction));
    }
    else if (req.Api == "open")
    {
        rsp.call_code = static_cast<DWORD>(api.open_transacted(&key, access, &oa, transaction));
    }
    else
    {
        ULONG disposition = 0;
        rsp.call_code = static_cast<DWORD>(
            api.create_transacted(&key, access, &oa, 0, nullptr, REG_OPTION_NON_VOLATILE, transaction, &disposition));
        if (NT_SUCCESS(static_cast<NTSTATUS>(rsp.call_code)))
        {
            rsp.disposition = disposition;
        }
    }

    if (key != nullptr)
    {
        /* A key handle of the NT API is the `HKEY` of the registry API. */
        const auto registry_key = reinterpret_cast<HKEY>(key);

        if (!value_name.empty() && !req.Data.empty())
        {
            rsp.write_code = WriteString(registry_key, value_name, appbox::UTF8ToWide(req.Data));
        }

        if (req.ReadBack)
        {
            rsp.read_code = ReadString(registry_key, value_name, rsp.readback);
        }

        /* The handle is closed before the transaction ends: an open transacted
         * handle keeps the transaction of the key alive. */
        RegCloseKey(registry_key);
    }

    if (req.End == "commit")
    {
        rsp.end_code = api.commit_transaction(transaction) ? ERROR_SUCCESS : GetLastError();
    }
    else if (req.End == "rollback")
    {
        rsp.end_code = api.rollback_transaction(transaction) ? ERROR_SUCCESS : GetLastError();
    }
    else
    {
        /* Closing the last handle of a transaction commits it. */
        rsp.end_code = CloseHandle(transaction) ? ERROR_SUCCESS : GetLastError();
    }

    if (req.ReadAfter)
    {
        HKEY after_key = nullptr;
        rsp.after_open_code = RegOpenKeyExW(view_root, key_path.c_str(), 0, KEY_READ, &after_key);
        if (rsp.after_open_code == ERROR_SUCCESS)
        {
            rsp.after_read_code = ReadString(after_key, value_name, rsp.after);
            RegCloseKey(after_key);
        }
    }

    if (mount_root != nullptr)
    {
        RegCloseKey(mount_root);
    }
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegTransacted("RegTransacted", ProbeRegTransacted_Entry);
