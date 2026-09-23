#include "utils/WinAPI.h" /* Must be first include file */
#include <fstream>
#include <vector>
#include "hook/NtCreateKey.hpp"
#include "hook/NtDeleteKey.hpp"
#include "hook/NtDeleteValueKey.hpp"
#include "hook/NtEnumerateKey.hpp"
#include "hook/NtEnumerateValueKey.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtOpenKey.hpp"
#include "hook/NtOpenKeyEx.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtQueryKey.hpp"
#include "hook/NtQueryMultipleValueKey.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryValueKey.hpp"
#include "hook/NtSaveKey.hpp"
#include "hook/NtSaveKeyEx.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "registry/EnumMerge.hpp"
#include "registry/IsolationPolicy.hpp"
#include "registry/KeyGuard.hpp"
#include "registry/KeyPath.hpp"
#include "registry/RootMap.hpp"
#include "registry/Whiteout.hpp"
#include "utils/QueryHandlePath.hpp"
#include "utils/Log.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "__init__.hpp"

/**
 * @brief Advapi32 entry which mounts a private application hive.
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadappkeyw
 */
typedef LONG(WINAPI* T_RegLoadAppKeyW)(
    /* [IN] */  LPCWSTR lpFile,
    /* [OUT] */ PHKEY   phKey,
    /* [IN] */  REGSAM  samDesired,
    /* [IN] */  DWORD   dwFlags,
    /* [IN] */  DWORD   dwReserved);

/**
 * @brief Opens the access token of a process.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntopenprocesstoken
 */
typedef NTSTATUS (*T_NtOpenProcessToken)(
    /* [IN] */  HANDLE      ProcessHandle,
    /* [IN] */  ACCESS_MASK DesiredAccess,
    /* [OUT] */ PHANDLE     TokenHandle);

/**
 * @brief Queries the access token of a process.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationtoken
 */
typedef NTSTATUS (*T_NtQueryInformationToken)(
    /* [IN] */       HANDLE Handle,
    /* [IN] */       ULONG  TokenInformationClass,
    /* [OUT] */      PVOID  TokenInformation,
    /* [IN] */       ULONG  TokenInformationLength,
    /* [OUT,OPT] */  PULONG ReturnLength);

/**
 * @brief Converts a SID into its string representation.
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlconvertsidtounicodestring
 */
typedef NTSTATUS (*T_RtlConvertSidToUnicodeString)(
    /* [OUT] */ PUNICODE_STRING UnicodeString,
    /* [IN] */  PVOID           Sid,
    /* [IN] */  BOOLEAN         AllocateDestinationString);

/**
 * @brief Frees a buffer which was allocated by RtlConvertSidToUnicodeString().
 */
typedef void (*T_RtlFreeUnicodeStringLocal)(PUNICODE_STRING UnicodeString);

/**
 * @brief The kernel object name of the root of every application hive.
 */
static const wchar_t* s_app_hive_root_name = L"\\REGISTRY\\A";

/**
 * @brief Runtime state of the registry hive module.
 */
struct appbox::registry::Hive::Data
{
    HANDLE         hive_root = nullptr; /* Root key handle of the private hive mount. */
    std::wstring   hive_mount_name;     /* Object name of the mount, for example \REGISTRY\A\{GUID}. */
    std::wstring   hkcu_prefix;         /* NT path of the real HKCU root, \REGISTRY\USER\<SID>. */
    std::wstring   hive_path;           /* DOS path of the hive file. */
    std::wstring   isolation_path;      /* DOS path of the isolation file. */
    IsolationTable isolation;           /* Isolation modes of the virtual registry. */

    /**
     * @brief Whether the whiteout store may hold a marker.
     *
     * The sandbox is the only writer of the store, so a store which does not
     * exist when the hive is mounted stays empty for the whole run: every
     * whiteout lookup is skipped and a sandbox which never deleted anything
     * pays nothing for the feature.
     */
    bool whiteout_possible = false;

    /**
     * @brief The entry point which mounts a private application hive.
     *
     * Resolved while the module initializes, so the save snapshot can mount its
     * temporary hive while the hooks are already attached.
     */
    T_RegLoadAppKeyW load_app_key = nullptr;

    /**
     * @brief The raw entry point which writes a value.
     *
     * The value level API is not hooked (a value write lands in the hive by
     * construction), so the module resolves the entry point for the internal
     * writes of the whiteout store and of the save snapshot.
     */
    T_NtSetValueKey set_value_key = nullptr;
};

/**
 * @brief Global state of the registry module. Null before Init() or after Exit().
 */
static appbox::registry::Hive::Data* s_hive_data = nullptr;

/**
 * @brief Close a handle with a locally resolved NtClose.
 *
 * The module is initialized before the hook table resolved the entry points,
 * so the sandbox wide sys_NtClose pointer is not available yet.
 *
 * @param[in] handle The handle to close.
 * @return Status code.
 */
static NTSTATUS CloseLocal(HANDLE handle)
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return STATUS_DLL_NOT_FOUND;
    }

    auto fn = reinterpret_cast<NTSTATUS (*)(HANDLE)>(GetProcAddress(ntdll, "NtClose"));
    if (fn == nullptr)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }
    return fn(handle);
}

/**
 * @brief Close a key handle which the registry module created itself.
 *
 * The module closes the handles of its own bookkeeping through the unhooked
 * NtClose entry point when the hook table is available and resolves the
 * function locally before that, so a close never re-enters a detour.
 *
 * @param[in] handle The handle to close, may be null.
 */
static void CloseHiveHandle(HANDLE handle)
{
    if (handle == nullptr)
    {
        return;
    }

    if (sys_NtClose != nullptr)
    {
        sys_NtClose(handle);
        return;
    }

    CloseLocal(handle);
}

/**
 * @brief Resolve an entry point of ntdll.
 *
 * The module needs entry points which are not part of the hook table: they are
 * resolved while the module initializes, which happens before the hooks are
 * attached, so a resolved pointer never points at a detour.
 *
 * @param[in] name Name of the entry point, for example `NtSetValueKey`.
 * @return The address of the entry point, null when it does not exist.
 */
static void* ResolveNtdllEntry(const char* name)
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(ntdll, name));
}

/**
 * @brief Query the kernel object name of a handle with a locally resolved NtQueryObject.
 * @param[in] handle The handle to query.
 * @param[out] name The object name.
 * @return true on success.
 */
static bool QueryObjectName(HANDLE handle, std::wstring& name)
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return false;
    }

    auto fn = reinterpret_cast<T_NtQueryObject>(GetProcAddress(ntdll, "NtQueryObject"));
    if (fn == nullptr)
    {
        return false;
    }

    ULONG needed = 0;
    if (!NT_SUCCESS(fn(handle, ObjectNameInformation, nullptr, 0, &needed)) && needed == 0)
    {
        needed = sizeof(OBJECT_NAME_INFORMATION) + 0x400;
    }

    std::vector<BYTE> buf(needed);
    NTSTATUS          st = fn(handle, ObjectNameInformation, buf.data(), (ULONG)buf.size(), &needed);
    if (!NT_SUCCESS(st))
    {
        return false;
    }

    auto* info = reinterpret_cast<OBJECT_NAME_INFORMATION*>(buf.data());
    if (info->Name.Buffer == nullptr || info->Name.Length == 0)
    {
        return false;
    }

    name.assign(info->Name.Buffer, info->Name.Length / sizeof(wchar_t));
    return true;
}

/**
 * @brief Build the NT path prefix of the HKCU of the current process.
 * @param[out] prefix The prefix, \REGISTRY\USER\<SID>.
 * @return true on success.
 */
static bool QueryHkcuPrefix(std::wstring& prefix)
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return false;
    }

    auto fn_open_token = reinterpret_cast<T_NtOpenProcessToken>(GetProcAddress(ntdll, "NtOpenProcessToken"));
    auto fn_query_token =
        reinterpret_cast<T_NtQueryInformationToken>(GetProcAddress(ntdll, "NtQueryInformationToken"));
    auto fn_sid_to_string =
        reinterpret_cast<T_RtlConvertSidToUnicodeString>(GetProcAddress(ntdll, "RtlConvertSidToUnicodeString"));
    auto fn_free_string = reinterpret_cast<T_RtlFreeUnicodeStringLocal>(GetProcAddress(ntdll, "RtlFreeUnicodeString"));
    if (fn_open_token == nullptr || fn_query_token == nullptr || fn_sid_to_string == nullptr ||
        fn_free_string == nullptr)
    {
        return false;
    }

    HANDLE token = nullptr;
    if (!NT_SUCCESS(fn_open_token((HANDLE)-1, TOKEN_QUERY, &token)))
    {
        return false;
    }

    /* TokenUser == 1 */
    BYTE  buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE] = {};
    ULONG len = 0;
    if (!NT_SUCCESS(fn_query_token(token, 1, buf, sizeof(buf), &len)))
    {
        CloseLocal(token);
        return false;
    }

    UNICODE_STRING sid_string;
    auto*          user = reinterpret_cast<TOKEN_USER*>(buf);
    if (!NT_SUCCESS(fn_sid_to_string(&sid_string, user->User.Sid, TRUE)))
    {
        CloseLocal(token);
        return false;
    }

    prefix.assign(sid_string.Buffer, sid_string.Length / sizeof(wchar_t));
    fn_free_string(&sid_string);
    CloseLocal(token);

    prefix.insert(0, L"\\REGISTRY\\USER\\");
    return true;
}

/**
 * @brief Create the five root keys of the view inside the hive.
 *
 * The hive of the view holds one sub key per root key, so opening a root key
 * lands inside the hive as well: a sandbox which created its own hive (an
 * archive without a packed registry) then behaves exactly like one with a
 * packed registry, and a handle of a root key is never a handle of the real
 * registry.
 *
 * The function runs before the hooks are attached, so it resolves the NT entry
 * points it needs locally instead of using the hook table.
 *
 * @param[in] hive_root The root key handle of the mount.
 * @return Status code.
 */
static NTSTATUS CreateRootKeys(HANDLE hive_root)
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return STATUS_DLL_NOT_FOUND;
    }

    auto fn_create_key = reinterpret_cast<T_NtCreateKey>(GetProcAddress(ntdll, "NtCreateKey"));
    if (fn_create_key == nullptr)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    for (const auto& root_name : appbox::registry::HiveRootKeyNames())
    {
        UNICODE_STRING    us;
        OBJECT_ATTRIBUTES oa;
        us.Buffer        = const_cast<PWSTR>(root_name.c_str());
        us.Length        = static_cast<USHORT>(root_name.size() * sizeof(wchar_t));
        us.MaximumLength = static_cast<USHORT>(us.Length + sizeof(wchar_t));
        InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, hive_root, nullptr);

        HANDLE   key = nullptr;
        ULONG    disposition = 0;
        NTSTATUS status =
            fn_create_key(&key, KEY_ALL_ACCESS, &oa, 0, nullptr, REG_OPTION_NON_VOLATILE, &disposition);
        if (!NT_SUCCESS(status))
        {
            LOG_E("failed to create the root key {} in the hive: {:#x}", appbox::WideToUTF8(root_name), status);
            return status;
        }

        CloseLocal(key);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Load the isolation modes of the virtual registry.
 *
 * A missing file is the normal case of an archive which carries no modes and a
 * malformed file is reported and ignored. Neither of them may fail the start
 * of the sandbox, because a sandbox without modes is exactly the read through
 * behaviour of a sandbox without an isolation file.
 *
 * @param[in,out] data The runtime state which receives the modes.
 */
static void LoadIsolationTable(appbox::registry::Hive::Data& data)
{
    if (data.isolation_path.empty())
    {
        LOG_D("no registry isolation file is configured");
        return;
    }

    std::ifstream stream(data.isolation_path, std::ios::binary);
    if (!stream.is_open())
    {
        LOG_D("the registry isolation file does not exist: {}", appbox::WideToUTF8(data.isolation_path));
        return;
    }

    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::string       error;
    if (!data.isolation.Parse(text, error))
    {
        LOG_W("the registry isolation file is ignored: {}", error);
        return;
    }

    LOG_I("registry isolation modes loaded: {} keys, {} values", data.isolation.KeyCount(),
          data.isolation.ValueCount());
}

/**
 * @brief Create or open a key relative to a parent handle of the hive.
 * @see CreateHiveKeyRelative()
 */
static NTSTATUS CreateHiveKeyRelative(HANDLE parent, const std::wstring& name, ACCESS_MASK DesiredAccess,
                                      ULONG Attributes, PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                      ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PHANDLE KeyHandle,
                                      PULONG Disposition);

/**
 * @brief Open a key of the sandbox hive when the hive holds it.
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] access The requested access mask.
 * @param[out] key The handle of the key, null when the hive does not hold it.
 * @return true when the key was opened.
 */
static bool TryOpenHiveKey(const std::wstring& relative, ACCESS_MASK access, HANDLE& key)
{
    key = nullptr;
    return NT_SUCCESS(appbox::registry::Hive::OpenKey(relative, access, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &key));
}

/**
 * @brief Whether a key handle holds a value.
 *
 * The value is queried with the basic information class into a local buffer: a
 * status which reports that the buffer is too small still proves that the
 * value exists, and only `STATUS_OBJECT_NAME_NOT_FOUND` reports a missing
 * value.
 *
 * @param[in] key The key handle, may be null.
 * @param[in] value_name Name of the value, empty for the default value.
 * @return true when the key holds the value.
 */
static bool HiveHoldsValue(HANDLE key, const std::wstring& value_name)
{
    if (key == nullptr)
    {
        return false;
    }

    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());

    BYTE   buffer[sizeof(KEY_VALUE_BASIC_INFORMATION) + 0x100] = {};
    ULONG  result = 0;
    const NTSTATUS status =
        sys_NtQueryValueKey(key, &us, KeyValueBasicInformation, buffer, sizeof(buffer), &result);
    return NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL;
}

/**
 * @brief Whether the whiteout store of the hive exists.
 *
 * The store is created by the first whiteout, so a missing store means that
 * the sandbox never deleted anything and no whiteout lookup has to run.
 *
 * The function runs while the module initializes, which happens before the
 * hook table resolved its entry points, so it resolves the two NT functions it
 * needs locally instead of using the `sys_*` pointers.
 *
 * @return true when the store exists.
 */
static bool WhiteoutStoreExists()
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return false;
    }

    auto fn_open = reinterpret_cast<T_NtOpenKey>(GetProcAddress(ntdll, "NtOpenKey"));
    auto fn_init = reinterpret_cast<T_RtlInitUnicodeString>(GetProcAddress(ntdll, "RtlInitUnicodeString"));
    if (fn_open == nullptr || fn_init == nullptr)
    {
        return false;
    }

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    fn_init(&us, appbox::registry_whiteout::kStoreKey);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, s_hive_data->hive_root, nullptr);

    HANDLE store = nullptr;
    if (!NT_SUCCESS(fn_open(&store, KEY_QUERY_VALUE, &oa)))
    {
        return false;
    }

    CloseLocal(store);
    return true;
}

/**
 * @brief Record that a key was deleted inside the sandbox.
 *
 * @param[in] relative The hive relative path of the deleted key.
 * @return true when the marker was written.
 */
static bool RecordKeyWhiteout(const std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    HANDLE   marker = nullptr;
    NTSTATUS status = appbox::registry::Hive::CreateKey(appbox::registry::WhiteoutKeyPath(relative), KEY_ALL_ACCESS,
                                                       OBJ_CASE_INSENSITIVE, nullptr, nullptr, 0, nullptr,
                                                       REG_OPTION_NON_VOLATILE, &marker, nullptr);
    if (!NT_SUCCESS(status))
    {
        LOG_E("failed to record the whiteout of the key {}: {:#x}", appbox::WideToUTF8(relative), status);
        return false;
    }

    CloseHiveHandle(marker);
    s_hive_data->whiteout_possible = true;
    LOG_I("the key {} was deleted inside the sandbox", appbox::WideToUTF8(relative));
    return true;
}

/**
 * @brief Record that a value was deleted inside the sandbox.
 *
 * The marker is a value of the marker key of the key which held the value, so
 * the name of the deleted value is stored verbatim, the empty name of the
 * default value included.
 *
 * @param[in] relative The hive relative path of the key which held the value.
 * @param[in] value_name Name of the deleted value, empty for the default value.
 * @return true when the marker was written.
 */
static bool RecordValueWhiteout(const std::wstring& relative, const std::wstring& value_name)
{
    if (s_hive_data == nullptr || s_hive_data->set_value_key == nullptr)
    {
        return false;
    }

    HANDLE   marker = nullptr;
    NTSTATUS status = appbox::registry::Hive::CreateKey(appbox::registry::WhiteoutValueKeyPath(relative),
                                                       KEY_SET_VALUE | KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr,
                                                       nullptr, 0, nullptr, REG_OPTION_NON_VOLATILE, &marker, nullptr);
    if (!NT_SUCCESS(status))
    {
        LOG_E("failed to record the whiteout of the value {} of {}: {:#x}", appbox::WideToUTF8(value_name),
              appbox::WideToUTF8(relative), status);
        return false;
    }

    const BYTE     payload = 1;
    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());
    status = s_hive_data->set_value_key(marker, &us, 0, REG_DWORD, const_cast<BYTE*>(&payload), sizeof(payload));
    CloseHiveHandle(marker);

    if (!NT_SUCCESS(status))
    {
        LOG_E("failed to record the whiteout of the value {} of {}: {:#x}", appbox::WideToUTF8(value_name),
              appbox::WideToUTF8(relative), status);
        return false;
    }

    s_hive_data->whiteout_possible = true;
    LOG_I("the value {} of {} was deleted inside the sandbox", appbox::WideToUTF8(value_name),
          appbox::WideToUTF8(relative));
    return true;
}

/**
 * @brief Whether an entry of the real layer stays invisible for the view.
 *
 * The rule has two halves, and both of them are the same for the merged
 * enumeration, the merged counts and the value read through: the isolation
 * mode of the entry may keep the host entry invisible, and a whiteout of the
 * sandbox may have deleted it. A key which was deleted hides its whole real
 * layer, so the check runs before the entry itself is classified.
 *
 * @param[in] relative The hive relative path of the parent key.
 * @param[in] values true tests a value name, false a sub key name.
 * @param[in] name Name of the entry inside the real key.
 * @return true when the entry must not be part of the merged view.
 */
static bool IsHiddenRealEntry(const std::wstring& relative, bool values, const std::wstring& name)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    if (appbox::registry::Hive::IsKeyWhitedOut(relative))
    {
        /* The key itself was deleted: its whole real layer is gone. */
        return true;
    }

    if (values)
    {
        return appbox::registry::IsolationTable::HidesHost(
                   appbox::registry::Hive::ValueIsolation(relative, name)) ||
               appbox::registry::Hive::IsValueWhitedOut(relative, name);
    }

    const std::wstring child = appbox::registry::JoinKeyPath(relative, name);
    return appbox::registry::IsolationTable::HidesHost(appbox::registry::Hive::KeyIsolation(child)) ||
           appbox::registry::Hive::IsKeyWhitedOut(child);
}

/**
 * @brief Read the type and the raw data of a value.
 *
 * @param[in] key The key handle which holds the value.
 * @param[in] value_name Name of the value, empty for the default value.
 * @param[out] type The `REG_*` type code of the value.
 * @param[out] data The raw data of the value.
 * @return Status code.
 */
static NTSTATUS ReadValueFull(HANDLE key, const std::wstring& value_name, ULONG& type, std::vector<BYTE>& data)
{
    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());

    std::vector<BYTE> buffer(sizeof(KEY_VALUE_FULL_INFORMATION) + 0x200);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        ULONG          result = 0;
        const NTSTATUS status = sys_NtQueryValueKey(key, &us, KeyValueFullInformation, buffer.data(),
                                                    static_cast<ULONG>(buffer.size()), &result);
        if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL)
        {
            if (result <= buffer.size())
            {
                /* Defensive: the reported size does not help, report the failure. */
                return status;
            }
            buffer.resize(result);
            continue;
        }
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        const auto* info = reinterpret_cast<const KEY_VALUE_FULL_INFORMATION*>(buffer.data());
        const auto  begin = static_cast<std::size_t>(info->DataOffset);
        const auto  end = begin + static_cast<std::size_t>(info->DataLength);
        if (begin > buffer.size() || end > buffer.size())
        {
            return STATUS_INVALID_BUFFER_SIZE;
        }

        type = info->Type;
        data.assign(buffer.begin() + static_cast<std::ptrdiff_t>(begin),
                    buffer.begin() + static_cast<std::ptrdiff_t>(end));
        return STATUS_SUCCESS;
    }

    return STATUS_BUFFER_TOO_SMALL;
}

/**
 * @brief Write a value with its raw data into a key of the hive.
 *
 * @param[in] key The key handle which receives the value.
 * @param[in] value_name Name of the value, empty for the default value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data The raw data of the value.
 * @return Status code.
 */
static NTSTATUS WriteValue(HANDLE key, const std::wstring& value_name, ULONG type, const std::vector<BYTE>& data)
{
    if (s_hive_data == nullptr || s_hive_data->set_value_key == nullptr)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());

    /* The entry point rejects a null data pointer, which an empty value has. */
    static BYTE empty = 0;
    PVOID       payload = data.empty() ? &empty : const_cast<BYTE*>(data.data());
    return s_hive_data->set_value_key(key, &us, 0, type, payload, static_cast<ULONG>(data.size()));
}

/**
 * @brief The last component of a hive relative key path.
 *
 * @param[in] relative The key path relative to the hive root.
 * @return The name of the key.
 */
static std::wstring LastKeyComponent(const std::wstring& relative)
{
    const auto separator = relative.find_last_of(L'\\');
    if (separator == std::wstring::npos)
    {
        return relative;
    }
    return relative.substr(separator + 1);
}

/**
 * @brief One entry of the merged view which a snapshot copies.
 */
struct MergedEntry
{
    std::wstring name;                 /* Name of the entry inside its layer. */
    HANDLE       handle = nullptr;     /* Handle which holds the entry. */
    bool         real = false;         /* The entry comes from the real layer. */
};

/**
 * @brief Resolve one entry of the merged view of a key.
 *
 * The entry is looked up through the merged index of the view (see
 * `Hive::ResolveMergedIndex`), so the snapshot copies exactly the entries the
 * sandboxed process sees.
 *
 * @param[in] hive_key The hive layer handle of the key, may be null.
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] values true resolves a value, false a sub key.
 * @param[in] index The index inside the merged view.
 * @param[out] entry The resolved entry. A real entry owns its handle and the
 *                   caller has to close it.
 * @return Status code, STATUS_NO_MORE_ENTRIES past the end of the view.
 */
static NTSTATUS ResolveMergedEntry(HANDLE hive_key, const std::wstring& view_path, bool values, ULONG index,
                                   MergedEntry& entry)
{
    HANDLE real = nullptr;
    ULONG  layer_index = 0;

    const appbox::registry::MergedResolve resolve =
        appbox::registry::Hive::ResolveMergedIndex(hive_key, view_path, values, index, real, layer_index);
    if (resolve == appbox::registry::MergedResolve::NoMoreEntries)
    {
        return STATUS_NO_MORE_ENTRIES;
    }
    if (resolve == appbox::registry::MergedResolve::Error)
    {
        return STATUS_UNSUCCESSFUL;
    }

    entry.real = resolve == appbox::registry::MergedResolve::RealLayer;
    entry.handle = entry.real ? real : hive_key;

    std::vector<std::wstring> names;
    const NTSTATUS status = values ? appbox::registry::Hive::CollectValueNames(entry.handle, names)
                                   : appbox::registry::Hive::CollectSubKeyNames(entry.handle, names);
    if (!NT_SUCCESS(status) || layer_index >= names.size())
    {
        if (entry.real)
        {
            sys_NtClose(real);
        }
        return NT_SUCCESS(status) ? STATUS_UNSUCCESSFUL : status;
    }

    entry.name = names[layer_index];
    return STATUS_SUCCESS;
}

/**
 * @brief Copy the merged view of a key into a key of the snapshot hive.
 *
 * @param[in] hive_key The hive layer handle of the key, may be null.
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] relative The key path relative to the hive root.
 * @param[in] destination The key of the snapshot hive which receives the content.
 * @param[in] depth The recursion depth of the copy.
 * @return Status code.
 */
static NTSTATUS CopyMergedKey(HANDLE hive_key, const std::wstring& view_path, const std::wstring& relative,
                              HANDLE destination, ULONG depth)
{
    /** Deepest key path the snapshot follows, a guard against a hostile hive. */
    constexpr ULONG kMaxDepth = 256;
    if (depth > kMaxDepth)
    {
        return STATUS_OBJECT_PATH_INVALID;
    }

    for (ULONG index = 0;; ++index)
    {
        MergedEntry entry;
        NTSTATUS   status = ResolveMergedEntry(hive_key, view_path, true, index, entry);
        if (status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        ULONG             type = REG_NONE;
        std::vector<BYTE> data;
        status = ReadValueFull(entry.handle, entry.name, type, data);
        if (entry.real)
        {
            sys_NtClose(entry.handle);
        }
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        status = WriteValue(destination, entry.name, type, data);
        LOG_D("snapshot of {}: value {} type {} size {} -> {:#x}", appbox::WideToUTF8(relative),
              appbox::WideToUTF8(entry.name), type, data.size(), status);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    for (ULONG index = 0;; ++index)
    {
        MergedEntry entry;
        NTSTATUS   status = ResolveMergedEntry(hive_key, view_path, false, index, entry);
        if (status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        const std::wstring child_view = appbox::registry::JoinKeyPath(view_path, entry.name);
        const std::wstring child_relative = appbox::registry::JoinKeyPath(relative, entry.name);

        LOG_D("snapshot of {}: sub key {} (real {})", appbox::WideToUTF8(relative), appbox::WideToUTF8(entry.name),
              entry.real);

        /*
         * A child of the hive layer is opened from the hive, a child which only
         * the real layer holds has no hive key at all: the merge answered it
         * from the real layer because the hive does not shadow the name.
         */
        HANDLE child_hive = nullptr;
        if (!entry.real)
        {
            if (!TryOpenHiveKey(child_relative, KEY_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, child_hive))
            {
                /* The hive holds the child, so the snapshot would be incomplete. */
                return STATUS_UNSUCCESSFUL;
            }
        }
        else
        {
            sys_NtClose(entry.handle);
        }

        HANDLE   child_destination = nullptr;
        NTSTATUS create_status = CreateHiveKeyRelative(destination, entry.name, KEY_ALL_ACCESS, OBJ_CASE_INSENSITIVE,
                                                       nullptr, nullptr, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                                       &child_destination, nullptr);
        if (NT_SUCCESS(create_status))
        {
            create_status = CopyMergedKey(child_hive, child_view, child_relative, child_destination, depth + 1);
        }

        CloseHiveHandle(child_destination);
        CloseHiveHandle(child_hive);
        if (!NT_SUCCESS(create_status))
        {
            return create_status;
        }
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Build the DOS path of the temporary hive of a save snapshot.
 *
 * The file is created next to the sandbox hive, so it lands in the overlay the
 * sandbox belongs to and disappears with it.
 *
 * @return The DOS path of the temporary hive.
 */
static std::wstring BuildSaveHivePath()
{
    std::wstring directory = s_hive_data != nullptr ? s_hive_data->hive_path : std::wstring();
    const auto   separator = directory.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        directory.clear();
    }
    else
    {
        directory.erase(separator + 1);
    }

    static volatile LONG sequence = 0;
    const LONG           id = InterlockedIncrement(&sequence);
    return directory + L"appbox-save-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(id) + L".hiv";
}

/**
 * @brief Remove one file of the overlay through the raw delete entry point.
 *
 * The temporary hive of a save is created by the mount of a hive file, which
 * resolves its path outside the filesystem view: the file lives at the path of
 * the overlay and the mount reads it there. The delete therefore runs through
 * the raw entry point as well, because a delete through the filesystem hooks
 * would be redirected into the view: it would leave the file of the overlay
 * behind and record a whiteout for a file which never existed in the view.
 *
 * @param[in] dos_path The DOS path of the file.
 */
static void DeleteOverlayFile(const std::wstring& dos_path)
{
    if (dos_path.empty() || sys_NtDeleteFile == nullptr)
    {
        return;
    }

    const std::wstring nt_path = L"\\??\\" + dos_path;

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, nt_path.c_str());
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    const NTSTATUS status = sys_NtDeleteFile(&oa);
    if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_NOT_FOUND && status != STATUS_OBJECT_PATH_NOT_FOUND)
    {
        LOG_W("failed to remove the temporary hive file {}: {:#x}", appbox::WideToUTF8(dos_path), status);
    }
}

/**
 * @brief Remove the temporary hive of a save snapshot.
 *
 * The mount creates two transaction logs next to the file, so they are removed
 * as well. A missing file is not an error: the snapshot removes a leftover of
 * an earlier run before it mounts its own file.
 *
 * @param[in] path The DOS path of the temporary hive.
 */
static void DeleteSaveHiveFiles(const std::wstring& path)
{
    DeleteOverlayFile(path);
    DeleteOverlayFile(path + L".LOG1");
    DeleteOverlayFile(path + L".LOG2");
}

NTSTATUS appbox::registry::Hive::Init()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        /* Nothing to do outside isolation mode; the hooks are not attached. */
        return STATUS_SUCCESS;
    }

    if (appbox::sandbox->wRegistryHiveDOSPath.empty())
    {
        LOG_E("the registry hive path is missing in the injected configuration");
        return STATUS_INVALID_PARAMETER;
    }

    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32 == nullptr)
    {
        LOG_E("failed to load advapi32.dll");
        return STATUS_DLL_NOT_FOUND;
    }

    auto fn_load_app_key = reinterpret_cast<T_RegLoadAppKeyW>(GetProcAddress(advapi32, "RegLoadAppKeyW"));
    if (fn_load_app_key == nullptr)
    {
        LOG_E("failed to resolve RegLoadAppKeyW");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    auto* data = new Data();
    data->hive_path = appbox::sandbox->wRegistryHiveDOSPath;
    data->load_app_key = fn_load_app_key;

    /*
     * The value level API is not hooked (a value write lands in the hive by
     * construction), so the internal writes of the module resolve their entry
     * point directly.
     */
    data->set_value_key = reinterpret_cast<T_NtSetValueKey>(ResolveNtdllEntry("NtSetValueKey"));
    if (data->set_value_key == nullptr)
    {
        LOG_E("failed to resolve NtSetValueKey");
        delete data;
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /*
     * Mount the hive. RegLoadAppKeyW creates the hive file when it does not
     * exist yet, so no template is needed. The hive is process private and
     * can only be opened relative to the returned root handle.
     */
    HKEY hive_root = nullptr;
    LONG err = fn_load_app_key(data->hive_path.c_str(), &hive_root, KEY_ALL_ACCESS, 0, 0);
    if (err != ERROR_SUCCESS)
    {
        LOG_E("RegLoadAppKeyW({}) failed: {}", appbox::WideToUTF8(data->hive_path), err);
        delete data;
        return STATUS_UNSUCCESSFUL;
    }
    data->hive_root = reinterpret_cast<HANDLE>(hive_root);

    /* Remember the private mount name, to recognize handles which already point into the hive. */
    if (!QueryObjectName(data->hive_root, data->hive_mount_name))
    {
        LOG_E("failed to query the object name of the hive root");
        CloseLocal(data->hive_root);
        delete data;
        return STATUS_UNSUCCESSFUL;
    }

    /* Collect the HKCU prefix of the current user. */
    if (!QueryHkcuPrefix(data->hkcu_prefix))
    {
        LOG_E("failed to query the HKCU prefix");
        CloseLocal(data->hive_root);
        delete data;
        return STATUS_UNSUCCESSFUL;
    }

    /* The five root keys of the view, so every root open lands in the hive. */
    NTSTATUS status = CreateRootKeys(data->hive_root);
    if (!NT_SUCCESS(status))
    {
        CloseLocal(data->hive_root);
        delete data;
        return status;
    }

    /* The modes of the virtual registry, which decide which host entries stay
     * invisible. A missing file keeps the default mode of every entry. */
    data->isolation_path = appbox::sandbox->wRegistryIsolationDOSPath;
    LoadIsolationTable(*data);

    s_hive_data = data;

    /*
     * A whiteout store which does not exist when the hive is mounted stays
     * empty for the whole run, because the sandbox is its only writer. The
     * flag keeps a sandbox which never deleted anything free of the lookups.
     */
    data->whiteout_possible = WhiteoutStoreExists();

    LOG_I("registry hive mounted: {} (mount: {}, hkcu: {}, whiteout store: {})",
          appbox::WideToUTF8(data->hive_path), appbox::WideToUTF8(data->hive_mount_name),
          appbox::WideToUTF8(data->hkcu_prefix), data->whiteout_possible);
    return STATUS_SUCCESS;
}

void appbox::registry::Hive::Exit()
{
    if (s_hive_data != nullptr)
    {
        if (s_hive_data->hive_root != nullptr)
        {
            CloseLocal(s_hive_data->hive_root);
        }
        delete s_hive_data;
        s_hive_data = nullptr;
    }
}

bool appbox::registry::Hive::IsEnabled()
{
    return s_hive_data != nullptr;
}

appbox::registry::HiveMap appbox::registry::Hive::MapKeyPath(POBJECT_ATTRIBUTES ObjectAttributes,
                                                             std::wstring& view_path, std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return HiveMap::NotIsolated;
    }

    std::wstring name;
    if (ObjectAttributes->ObjectName != nullptr && ObjectAttributes->ObjectName->Buffer != nullptr)
    {
        name.assign(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length / sizeof(wchar_t));
    }

    std::wstring path;
    if (ObjectAttributes->RootDirectory == nullptr)
    {
        /* Full path form: the object name is the complete NT path. */
        path = name;
    }
    else
    {
        /* Root relative form: query the path of the root handle and append the name. */
        std::wstring root;
        if (!NT_SUCCESS(appbox::QueryHandlePath(ObjectAttributes->RootDirectory, root)))
        {
            /* The path cannot be determined, forward the call unchanged. */
            return HiveMap::NotIsolated;
        }

        /*
         * Handles which were handed out by the hooks refer to keys inside the
         * hive. Translate them back into the view, so the key behaves exactly
         * like the key it shadows.
         */
        std::wstring hive_relative;
        std::wstring translated;
        if (appbox::registry::StripKeyPrefix(root, s_hive_data->hive_mount_name, hive_relative) &&
            appbox::registry::MapHivePathToView(hive_relative, s_hive_data->hkcu_prefix, translated))
        {
            path = translated;
        }
        else
        {
            path = root;
        }

        path = appbox::registry::JoinKeyPath(path, name);
    }

    if (!appbox::registry::MapViewPathToHive(path, s_hive_data->hkcu_prefix, relative))
    {
        return HiveMap::NotIsolated;
    }

    view_path = path;
    return HiveMap::Isolated;
}

bool appbox::registry::Hive::HiveRelativePath(const std::wstring& view_path, std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    return appbox::registry::MapViewPathToHive(view_path, s_hive_data->hkcu_prefix, relative);
}

appbox::RegistryIsolation appbox::registry::Hive::KeyIsolation(const std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return RegistryIsolation::WriteCopy;
    }

    return s_hive_data->isolation.KeyMode(relative);
}

appbox::RegistryIsolation appbox::registry::Hive::ValueIsolation(const std::wstring& relative,
                                                                const std::wstring& value_name)
{
    if (s_hive_data == nullptr)
    {
        return RegistryIsolation::WriteCopy;
    }

    return s_hive_data->isolation.ValueMode(relative, value_name);
}

bool appbox::registry::Hive::HidesHostValue(const std::wstring& relative, const std::wstring& value_name)
{
    return IsolationTable::HidesHost(ValueIsolation(relative, value_name));
}

bool appbox::registry::Hive::IsKeyWhitedOut(const std::wstring& relative)
{
    if (s_hive_data == nullptr || !s_hive_data->whiteout_possible)
    {
        /* Without a store the sandbox never deleted anything. */
        return false;
    }

    /*
     * The lookup walks the path upwards: the delete of a key removes
     * everything below it as well, so a marker of an ancestor hides the key.
     */
    std::vector<std::wstring> prefixes;
    appbox::registry::KeyPathPrefixes(relative, prefixes);
    for (const auto& prefix : prefixes)
    {
        HANDLE marker = nullptr;
        if (TryOpenHiveKey(appbox::registry::WhiteoutKeyPath(prefix), KEY_QUERY_VALUE, marker))
        {
            CloseHiveHandle(marker);
            return true;
        }
    }

    return false;
}

bool appbox::registry::Hive::IsValueWhitedOut(const std::wstring& relative, const std::wstring& value_name)
{
    if (s_hive_data == nullptr || !s_hive_data->whiteout_possible)
    {
        return false;
    }

    /* A deleted key hides every value it held. */
    if (IsKeyWhitedOut(relative))
    {
        return true;
    }

    HANDLE marker = nullptr;
    if (!TryOpenHiveKey(appbox::registry::WhiteoutValueKeyPath(relative), KEY_QUERY_VALUE, marker))
    {
        return false;
    }

    appbox::registry::KeyGuard guard(marker);
    return HiveHoldsValue(marker, value_name);
}

bool appbox::registry::ReadValueName(PUNICODE_STRING ValueName, std::wstring& name)
{
    name.clear();
    if (ValueName == nullptr || ValueName->Buffer == nullptr || ValueName->Length == 0)
    {
        /* A null name addresses the default value of the key. */
        return true;
    }

    /*
     * A caller can pass a length which does not fit into the buffer it owns.
     * Such a structure is not read at all; the kernel rejects the call, so the
     * isolation decision is left to the forwarded call.
     */
    if (ValueName->Length > ValueName->MaximumLength || (ValueName->Length % sizeof(wchar_t)) != 0)
    {
        return false;
    }

    name.assign(ValueName->Buffer, static_cast<std::size_t>(ValueName->Length) / sizeof(wchar_t));
    return true;
}

NTSTATUS appbox::registry::Hive::OpenKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                         PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                         PHANDLE KeyHandle)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, relative.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, s_hive_data->hive_root, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    return sys_NtOpenKey(KeyHandle, DesiredAccess, &oa);
}

NTSTATUS appbox::registry::Hive::OpenKeyEx(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                          PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                                          PHANDLE KeyHandle)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, relative.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, s_hive_data->hive_root, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    return sys_NtOpenKeyEx(KeyHandle, DesiredAccess, &oa, OpenOptions);
}

NTSTATUS appbox::registry::Hive::OpenRealKey(const std::wstring& view_path, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                             PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                             PHANDLE KeyHandle)
{
    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, view_path.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, nullptr, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    return sys_NtOpenKey(KeyHandle, DesiredAccess, &oa);
}

NTSTATUS appbox::registry::Hive::OpenRealKeyEx(const std::wstring& view_path, ACCESS_MASK DesiredAccess,
                                               ULONG Attributes, PVOID SecurityDescriptor,
                                               PVOID SecurityQualityOfService, ULONG OpenOptions,
                                               PHANDLE KeyHandle)
{
    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, view_path.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, nullptr, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    return sys_NtOpenKeyEx(KeyHandle, DesiredAccess, &oa, OpenOptions);
}

bool appbox::registry::Hive::HostHoldsKey(const std::wstring& view_path)
{
    /*
     * The mask is the pair the merged view uses to reach the host layer (the
     * merged counts of NtQueryKey and the merged enumeration of
     * ResolveMergedIndex), so a key which exists but denies that pair is
     * reported as missing — which is the answer the merged view would give.
     */
    HANDLE real = nullptr;
    if (!NT_SUCCESS(OpenRealKey(view_path, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, OBJ_CASE_INSENSITIVE, nullptr,
                                nullptr, &real)))
    {
        return false;
    }

    appbox::registry::KeyGuard guard(real);
    return true;
}

bool appbox::registry::Hive::HostHoldsValue(const std::wstring& view_path, const std::wstring& value_name)
{
    HANDLE real = nullptr;
    if (!NT_SUCCESS(OpenRealKey(view_path, KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
    {
        return false;
    }

    appbox::registry::KeyGuard guard(real);
    return HiveHoldsValue(real, value_name);
}

/**
 * @brief Open a key of the real registry through the entry point of the caller.
 *
 * The helper keeps the open policy readable: the policy only picks a layer,
 * the entry point of the hooked call decides which NT function performs the
 * open.
 *
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] OpenOptions The open options of the original call.
 * @param[in] extended true runs NtOpenKeyEx, false runs NtOpenKey.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenRealKeyEntry(const std::wstring& view_path, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                 PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                                 bool extended, PHANDLE KeyHandle)
{
    if (extended)
    {
        return appbox::registry::Hive::OpenRealKeyEx(view_path, DesiredAccess, Attributes, SecurityDescriptor,
                                                    SecurityQualityOfService, OpenOptions, KeyHandle);
    }

    return appbox::registry::Hive::OpenRealKey(view_path, DesiredAccess, Attributes, SecurityDescriptor,
                                              SecurityQualityOfService, KeyHandle);
}

/**
 * @brief Open a key of the sandbox hive through the entry point of the caller.
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] OpenOptions The open options of the original call.
 * @param[in] extended true runs NtOpenKeyEx, false runs NtOpenKey.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenHiveKeyEntry(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                 PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                                 bool extended, PHANDLE KeyHandle)
{
    if (extended)
    {
        return appbox::registry::Hive::OpenKeyEx(relative, DesiredAccess, Attributes, SecurityDescriptor,
                                                SecurityQualityOfService, OpenOptions, KeyHandle);
    }

    return appbox::registry::Hive::OpenKey(relative, DesiredAccess, Attributes, SecurityDescriptor,
                                          SecurityQualityOfService, KeyHandle);
}

/**
 * @brief The open policy of the registry isolation, shared by NtOpenKey and NtOpenKeyEx.
 *
 * The decision table is the one of registry/IsolationPolicy.hpp: the hive
 * layer is tried first and the isolation mode of the key decides about the
 * fallback. A key which the sandbox deleted (a whiteout) never reaches the
 * host layer: it does not exist in the view, so the failure of the hive open
 * is the result of the call.
 *
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] OpenOptions The open options of the original call.
 * @param[in] extended true runs the extended entry points, false the plain ones.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenIsolatedKeyEntry(const std::wstring& view_path, const std::wstring& relative,
                                     ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                     PVOID SecurityQualityOfService, ULONG OpenOptions, bool extended,
                                     PHANDLE KeyHandle)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    const appbox::RegistryIsolation mode = appbox::registry::Hive::KeyIsolation(relative);

    HANDLE   key = nullptr;
    NTSTATUS st_hive = OpenHiveKeyEntry(relative, DesiredAccess, Attributes, SecurityDescriptor,
                                        SecurityQualityOfService, OpenOptions, extended, &key);
    if (NT_SUCCESS(st_hive))
    {
        *KeyHandle = key;
        return st_hive;
    }

    /*
     * A key which the sandbox deleted does not exist in its view any more: the
     * host entry must neither be read through nor be copied up, so the failure
     * of the hive open is the result — the rule of `Full` and `Hide`.
     */
    if (appbox::registry::Hive::IsKeyWhitedOut(relative))
    {
        return st_hive;
    }

    switch (appbox::registry::FallbackForKey(mode, DesiredAccess))
    {
    case appbox::registry::OpenFallback::ReportHiveFailure:
        /* `Full` and `Hide`: the host entry does not exist for the sandbox. */
        return st_hive;

    case appbox::registry::OpenFallback::UseHost:
        return OpenRealKeyEntry(view_path, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService,
                                OpenOptions, extended, KeyHandle);

    case appbox::registry::OpenFallback::CopyUp:
    {
        /*
         * `WriteCopy` with write access: the caller would modify the host key
         * through a real handle, so the key is copied up instead. The shadow
         * key takes the place of the real key — the merged enumeration, the
         * read through of the values and the merged counts keep the entries of
         * the real key visible.
         */
        HANDLE   real = nullptr;
        NTSTATUS st_real = OpenRealKeyEntry(view_path, DesiredAccess, Attributes, SecurityDescriptor,
                                            SecurityQualityOfService, OpenOptions, extended, &real);
        if (!NT_SUCCESS(st_real))
        {
            /* The host does not hold the key either: an open never creates a
             * key, so the open fails. */
            return appbox::registry::PickOpenFailure(st_real, st_hive);
        }

        /* The real handle is only the proof that the host holds the key. */
        appbox::registry::KeyGuard guard(real);

        NTSTATUS st_copy = appbox::registry::Hive::CreateKey(relative, DesiredAccess, Attributes, SecurityDescriptor,
                                                             SecurityQualityOfService, 0, nullptr,
                                                             REG_OPTION_NON_VOLATILE, KeyHandle, nullptr);
        if (!NT_SUCCESS(st_copy))
        {
            /* Failing closed: a fallback to the real key would let the writes
             * of the caller escape into the host registry. */
            LOG_E("failed to copy the key {} up into the hive: {:#x}", appbox::WideToUTF8(relative), st_copy);
        }
        return st_copy;
    }
    }

    return st_hive;
}

NTSTATUS appbox::registry::Hive::OpenIsolatedKey(const std::wstring& view_path, const std::wstring& relative,
                                                 ACCESS_MASK DesiredAccess, ULONG Attributes,
                                                 PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                                 PHANDLE KeyHandle)
{
    return OpenIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                SecurityQualityOfService, 0, false, KeyHandle);
}

NTSTATUS appbox::registry::Hive::OpenIsolatedKeyEx(const std::wstring& view_path, const std::wstring& relative,
                                                   ACCESS_MASK DesiredAccess, ULONG Attributes,
                                                   PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                                   ULONG OpenOptions, PHANDLE KeyHandle)
{
    return OpenIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                SecurityQualityOfService, OpenOptions, true, KeyHandle);
}

/**
 * @brief Create or open a key of the hive relative to a parent handle.
 *
 * @param[in] parent The handle of the parent key.
 * @param[in] name The name of the key, a single component.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags.
 * @param[in] SecurityDescriptor The security descriptor, may be null.
 * @param[in] SecurityQualityOfService The quality of service, may be null.
 * @param[in] TitleIndex The title index of the original call.
 * @param[in] Class The key class of the original call, may be null.
 * @param[in] CreateOptions The create options of the original call.
 * @param[out] KeyHandle The resulting key handle.
 * @param[out] Disposition REG_CREATED_NEW_KEY or REG_OPENED_EXISTING_KEY, may be null.
 * @return Status code.
 */
static NTSTATUS CreateHiveKeyRelative(HANDLE parent, const std::wstring& name, ACCESS_MASK DesiredAccess,
                                      ULONG Attributes, PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                      ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PHANDLE KeyHandle,
                                      PULONG Disposition)
{
    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, name.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, parent, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    return sys_NtCreateKey(KeyHandle, DesiredAccess, &oa, TitleIndex, Class, CreateOptions, Disposition);
}

NTSTATUS appbox::registry::Hive::CreateKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                          PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex,
                                          PUNICODE_STRING Class, ULONG CreateOptions, PHANDLE KeyHandle,
                                          PULONG Disposition)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    std::vector<std::wstring> components;
    if (!appbox::registry::SplitKeyPath(relative, components))
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    /*
     * The path is walked component by component: a create of a sandboxed
     * process may name a path which only the real registry holds so far, and
     * the hive has to hold it afterwards. Every intermediate key is created
     * relative to its parent with full access; the last component is created
     * or opened with the mask of the caller and reports Disposition, so the
     * caller sees what RegCreateKeyEx reports without the isolation.
     */
    HANDLE              parent = s_hive_data->hive_root;
    std::vector<HANDLE> intermediates;

    for (std::size_t index = 0; index + 1 < components.size(); ++index)
    {
        HANDLE   child = nullptr;
        NTSTATUS status = CreateHiveKeyRelative(parent, components[index], KEY_ALL_ACCESS, Attributes,
                                                SecurityDescriptor, SecurityQualityOfService, 0, nullptr,
                                                REG_OPTION_NON_VOLATILE, &child, nullptr);
        if (!NT_SUCCESS(status))
        {
            for (const HANDLE handle : intermediates)
            {
                CloseHiveHandle(handle);
            }
            return status;
        }

        intermediates.push_back(child);
        parent = child;
    }

    /*
     * The right to create a sub key is added to the mask of the caller: the
     * kernel checks it against the parent of the key it creates, so a create
     * with a narrow mask (KEY_SET_VALUE of a write access open, for example)
     * would otherwise fail with a missing key. The extra right only widens the
     * handle of a key which lives inside the hive.
     */
    NTSTATUS status = CreateHiveKeyRelative(parent, components.back(), DesiredAccess | KEY_CREATE_SUB_KEY, Attributes,
                                            SecurityDescriptor, SecurityQualityOfService, TitleIndex, Class,
                                            CreateOptions, KeyHandle, Disposition);

    for (const HANDLE handle : intermediates)
    {
        CloseHiveHandle(handle);
    }

    return status;
}

NTSTATUS appbox::registry::Hive::CreateIsolatedKey(const std::wstring& view_path, const std::wstring& relative,
                                                   ACCESS_MASK DesiredAccess, ULONG Attributes,
                                                   PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                                   ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions,
                                                   PHANDLE KeyHandle, PULONG Disposition)
{
    const NTSTATUS status = CreateKey(relative, DesiredAccess, Attributes, SecurityDescriptor,
                                      SecurityQualityOfService, TitleIndex, Class, CreateOptions, KeyHandle,
                                      Disposition);
    if (!NT_SUCCESS(status) || Disposition == nullptr)
    {
        return status;
    }

    /*
     * The disposition of the hive describes the hive layer alone, while the
     * caller sees the merged view (see
     * appbox::registry::ViewCreateDisposition). The host layer is only asked
     * when it can change the answer: a key which the hive already held is
     * reported by the hive, a mode which keeps the host entry invisible has no
     * visible host key, and a key which the sandbox deleted stays invisible as
     * well. The short circuit keeps a create of a key which the hive holds
     * free of extra calls.
     *
     * The whiteout of a deleted key survives its recreation: the caller sees
     * an empty key, exactly like a delete followed by a create behaves in the
     * real registry, and the host content stays hidden.
     */
    const RegistryIsolation mode = KeyIsolation(relative);
    const bool host_holds_key = *Disposition == REG_CREATED_NEW_KEY
                                && !appbox::registry::IsolationTable::HidesHost(mode)
                                && !IsKeyWhitedOut(relative) && HostHoldsKey(view_path);

    *Disposition = appbox::registry::ViewCreateDisposition(mode, *Disposition, host_holds_key);
    return status;
}

appbox::registry::HandleView appbox::registry::Hive::MapHandleView(HANDLE KeyHandle, std::wstring& view_path)
{
    if (s_hive_data == nullptr)
    {
        return HandleView::NotIsolated;
    }

    std::wstring path;
    if (!NT_SUCCESS(appbox::QueryHandlePath(KeyHandle, path)))
    {
        return HandleView::NotIsolated;
    }

    std::wstring relative;
    if (appbox::registry::StripKeyPrefix(path, s_hive_data->hive_mount_name, relative))
    {
        std::wstring translated;
        if (!appbox::registry::MapHivePathToView(relative, s_hive_data->hkcu_prefix, translated))
        {
            /* The private mount root itself has no place in the view. */
            return HandleView::NotIsolated;
        }

        view_path = translated;
        return HandleView::HiveHandle;
    }

    std::wstring hive_relative;
    if (appbox::registry::MapViewPathToHive(path, s_hive_data->hkcu_prefix, hive_relative))
    {
        view_path = path;
        return HandleView::RealHandle;
    }

    return HandleView::NotIsolated;
}

bool appbox::registry::Hive::TranslateHiveObjectName(const std::wstring& object_name, std::wstring& view_name)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    std::wstring relative;
    if (!appbox::registry::StripKeyPrefix(object_name, s_hive_data->hive_mount_name, relative))
    {
        return false;
    }

    return appbox::registry::MapHivePathToView(relative, s_hive_data->hkcu_prefix, view_name);
}

void appbox::registry::Hive::FilterHiddenEntries(const std::wstring& view_path, bool values,
                                                 std::vector<std::wstring>& names)
{
    if (s_hive_data == nullptr || (s_hive_data->isolation.Empty() && !s_hive_data->whiteout_possible))
    {
        return;
    }

    std::wstring relative;
    if (!HiveRelativePath(view_path, relative))
    {
        /* Without a hive relative path the modes cannot be looked up, so the
         * layer is presented unchanged. */
        return;
    }

    /*
     * The merged enumeration and the merged counts of `NtQueryKey` share this
     * filter, so neither of them ever announces an entry the other one hides.
     */
    std::vector<std::wstring> visible;
    visible.reserve(names.size());
    for (const auto& name : names)
    {
        if (!IsHiddenRealEntry(relative, values, name))
        {
            visible.push_back(name);
        }
    }
    names.swap(visible);
}

NTSTATUS appbox::registry::Hive::CollectSubKeyNames(HANDLE KeyHandle, std::vector<std::wstring>& names)
{
    if (sys_NtEnumerateKey == nullptr)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /*
     * Registry key names are limited to 255 characters, so one page covers the
     * largest possible KEY_BASIC_INFORMATION entry. A single retry with the
     * reported size guards against longer names on future systems.
     */
    std::vector<BYTE> buf(sizeof(KEY_BASIC_INFORMATION) + 0x400);
    for (ULONG i = 0;; ++i)
    {
        ULONG    result = 0;
        NTSTATUS st = sys_NtEnumerateKey(KeyHandle, i, KeyBasicInformation, buf.data(), (ULONG)buf.size(), &result);
        if (st == STATUS_NO_MORE_ENTRIES)
        {
            return STATUS_SUCCESS;
        }
        if (st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL)
        {
            buf.resize(result);
            st = sys_NtEnumerateKey(KeyHandle, i, KeyBasicInformation, buf.data(), (ULONG)buf.size(), &result);
            if (st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL)
            {
                /* Even the reported size is not enough, report the failure. */
                return st;
            }
        }
        if (!NT_SUCCESS(st))
        {
            return st;
        }

        auto* info = reinterpret_cast<KEY_BASIC_INFORMATION*>(buf.data());

        /*
         * Nothing may be skipped here: the position of an entry inside this
         * collection is the index the kernel assigned to it, because the merged
         * enumeration feeds the position back to the enumeration of the layer
         * (see ResolveMergedIndex). A skipped entry would shift every following
         * entry and hide the last one.
         */
        names.emplace_back(info->Name, info->NameLength / sizeof(WCHAR));
    }
}

NTSTATUS appbox::registry::Hive::CollectValueNames(HANDLE KeyHandle, std::vector<std::wstring>& names)
{
    if (sys_NtEnumerateValueKey == nullptr)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /* Value names may reach 16383 characters, so the buffer grows on demand. */
    std::vector<BYTE> buf(sizeof(KEY_VALUE_BASIC_INFORMATION) + 0x800);
    for (ULONG i = 0;; ++i)
    {
        ULONG    result = 0;
        NTSTATUS st =
            sys_NtEnumerateValueKey(KeyHandle, i, KeyValueBasicInformation, buf.data(), (ULONG)buf.size(), &result);
        if (st == STATUS_NO_MORE_ENTRIES)
        {
            return STATUS_SUCCESS;
        }
        if (st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL)
        {
            buf.resize(result);
            st = sys_NtEnumerateValueKey(KeyHandle, i, KeyValueBasicInformation, buf.data(), (ULONG)buf.size(),
                                         &result);
            if (st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL)
            {
                /* Even the reported size is not enough, report the failure. */
                return st;
            }
        }
        if (!NT_SUCCESS(st))
        {
            return st;
        }

        auto* info = reinterpret_cast<KEY_VALUE_BASIC_INFORMATION*>(buf.data());

        /*
         * The default value of a key is enumerated as an entry with an empty
         * name and occupies an index like every other value, so it is part of
         * the collection. Nothing may be skipped here: the position of an entry
         * inside this collection is the index the kernel assigned to it,
         * because the merged enumeration feeds the position back to the
         * enumeration of the layer (see ResolveMergedIndex). Skipping the
         * default value would shift every following value — the last one would
         * never be reported — and it would drop the default value of the real
         * layer from the merged view.
         */
        names.emplace_back(info->Name, info->NameLength / sizeof(WCHAR));
    }
}

appbox::registry::MergedResolve appbox::registry::Hive::ResolveMergedIndex(HANDLE KeyHandle,
                                                                           const std::wstring& view_path, bool values,
                                                                           ULONG Index, HANDLE& real_handle,
                                                                           ULONG& layer_index)
{
    real_handle = nullptr;

    /*
     * The hive layer is optional as well: a key which the hive does not hold
     * has no hive handle, and its merged view is the visible real layer alone.
     * The snapshot of a save walks such subtrees.
     */
    std::vector<std::wstring> hive_names;
    if (KeyHandle != nullptr)
    {
        const NTSTATUS st =
            values ? CollectValueNames(KeyHandle, hive_names) : CollectSubKeyNames(KeyHandle, hive_names);
        if (!NT_SUCCESS(st))
        {
            return MergedResolve::Error;
        }
    }

    /*
     * The real layer is optional: when the real key does not exist or cannot
     * be read, the merged view degenerates to the hive layer alone, which is
     * still a consistent view.
     *
     * The names of the merged view are the entries which stay visible, while
     * the index of an entry inside the real key is the position it has in the
     * unfiltered enumeration of that key: the kernel is asked for the entry at
     * that position.
     *
     * Both collectors report every enumerated entry — the default value of a
     * key included, whose name is empty — so the position of an entry inside a
     * collected list is the index the kernel assigned to it. That is why the
     * merged index of a hive entry is forwarded as it is and why a visible
     * real entry carries its collected position as its layer index.
     */
    std::vector<std::wstring> visible_names;
    std::vector<ULONG>        visible_indices;
    HANDLE                    real = nullptr;
    ACCESS_MASK               mask = values ? KEY_QUERY_VALUE : KEY_ENUMERATE_SUB_KEYS;
    if (NT_SUCCESS(OpenRealKey(view_path, mask, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
    {
        std::vector<std::wstring> real_names;
        NTSTATUS st = values ? CollectValueNames(real, real_names) : CollectSubKeyNames(real, real_names);
        if (NT_SUCCESS(st))
        {
            std::wstring relative;
            const bool   filtered = HiveRelativePath(view_path, relative);

            visible_names.reserve(real_names.size());
            visible_indices.reserve(real_names.size());
            for (ULONG index = 0; index < real_names.size(); ++index)
            {
                if (filtered && IsHiddenRealEntry(relative, values, real_names[index]))
                {
                    continue;
                }

                visible_names.push_back(real_names[index]);
                visible_indices.push_back(index);
            }

            real_handle = real;
        }
        else
        {
            sys_NtClose(real);
        }
    }

    EnumLayer layer = EnumLayer::Hive;
    size_t    idx = 0;
    if (!appbox::registry::MapMergedIndex(hive_names, visible_names, Index, layer, idx))
    {
        if (real_handle != nullptr)
        {
            sys_NtClose(real_handle);
            real_handle = nullptr;
        }
        return MergedResolve::NoMoreEntries;
    }

    if (layer == EnumLayer::Real)
    {
        /* The index inside the real key, not the index inside the visible
         * subset of its entries. */
        layer_index = visible_indices[idx];
        return MergedResolve::RealLayer;
    }

    layer_index = (ULONG)idx;

    /* The entry lives in the hive layer, the real handle is not needed. */
    if (real_handle != nullptr)
    {
        sys_NtClose(real_handle);
        real_handle = nullptr;
    }
    return MergedResolve::HiveLayer;
}

/**
 * @brief Whether the merged view of a key holds a visible sub key.
 *
 * The kernel refuses to delete a key which holds sub keys, and the sandboxed
 * process sees the sub keys of both layers, so the check has to run against
 * the merged view. A layer which cannot be collected is treated as empty: the
 * delete of the hive key reports the real failure in that case.
 *
 * @param[in] hive_key The hive key handle, may be null.
 * @param[in] view_path The logical path of the key in the view.
 * @return true when the merged view holds at least one sub key.
 */
static bool MergedViewHoldsSubKeys(HANDLE hive_key, const std::wstring& view_path)
{
    std::vector<std::wstring> hive_names;
    if (hive_key != nullptr && !NT_SUCCESS(appbox::registry::Hive::CollectSubKeyNames(hive_key, hive_names)))
    {
        hive_names.clear();
    }

    std::vector<std::wstring> real_names;
    HANDLE                    real = nullptr;
    if (NT_SUCCESS(appbox::registry::Hive::OpenRealKey(view_path, KEY_ENUMERATE_SUB_KEYS, OBJ_CASE_INSENSITIVE,
                                                       nullptr, nullptr, &real)))
    {
        appbox::registry::KeyGuard guard(real);
        if (NT_SUCCESS(appbox::registry::Hive::CollectSubKeyNames(real, real_names)))
        {
            appbox::registry::Hive::FilterHiddenEntries(view_path, false, real_names);
        }
        else
        {
            real_names.clear();
        }
    }

    return appbox::registry::CountMerged(hive_names, real_names) > 0;
}

/**
 * @brief Delete a value of a key of the hive.
 *
 * @param[in] key The hive key handle which holds the value.
 * @param[in] value_name Name of the value, empty for the default value.
 * @return Status code.
 */
static NTSTATUS DeleteHiveValue(HANDLE key, const std::wstring& value_name)
{
    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());
    return sys_NtDeleteValueKey(key, &us);
}

NTSTATUS appbox::registry::Hive::DeleteIsolatedKey(HANDLE KeyHandle, const std::wstring& view_path,
                                                   const std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    /*
     * A key which still holds sub keys cannot be deleted, and the caller sees
     * the sub keys of both layers. Values do not block a delete. The check runs
     * before the kernel is asked, because the kernel only knows the sub keys of
     * the hive layer.
     */
    if (MergedViewHoldsSubKeys(KeyHandle, view_path))
    {
        return STATUS_CANNOT_DELETE;
    }

    /*
     * The kernel removes the key of the hive and checks the rights of the
     * handle of the caller, so a handle which does not permit a delete reports
     * the failure the real call would report.
     */
    const NTSTATUS status = sys_NtDeleteKey(KeyHandle);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    /*
     * The hive holds the key (the handle proves it), so the table reports
     * `HiveOnly` or `HiveAndWhiteout`. The host layer is only probed when the
     * mode keeps it visible, which is the only case the table consults it in.
     */
    const bool host_holds =
        !appbox::registry::IsolationTable::HidesHost(KeyIsolation(relative)) && HostHoldsKey(view_path);
    const appbox::registry::DeleteTarget target =
        appbox::registry::DeleteOutcomeOf(KeyIsolation(relative), true, host_holds);

    if (target == appbox::registry::DeleteTarget::HiveAndWhiteout && !RecordKeyWhiteout(relative))
    {
        /* Failing closed: reporting a success would let the key of the host
         * reappear in the view of the sandbox. */
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS appbox::registry::Hive::DeleteIsolatedValue(HANDLE KeyHandle, const std::wstring& view_path,
                                                     const std::wstring& relative, const std::wstring& value_name)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    /*
     * The kernel removes the value of the hive and checks the rights of the
     * handle of the caller; its status also tells whether the hive held the
     * value, which is what the delete route needs. Only a missing value is a
     * result of the isolation: every other failure is the answer of the real
     * call.
     */
    const NTSTATUS status = DeleteHiveValue(KeyHandle, value_name);
    if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return status;
    }

    const bool host_visible =
        !appbox::registry::IsolationTable::HidesHost(ValueIsolation(relative, value_name));
    const appbox::registry::DeleteTarget target = appbox::registry::DeleteOutcomeOf(
        ValueIsolation(relative, value_name), NT_SUCCESS(status),
        host_visible && HostHoldsValue(view_path, value_name));

    switch (target)
    {
    case appbox::registry::DeleteTarget::ReportMissing:
        /* The value does not exist in the view of the sandbox. */
        return STATUS_OBJECT_NAME_NOT_FOUND;

    case appbox::registry::DeleteTarget::HiveOnly:
        /* The value of the hive is gone and the host entry stays invisible. */
        return STATUS_SUCCESS;

    case appbox::registry::DeleteTarget::WhiteoutOnly:
    case appbox::registry::DeleteTarget::HiveAndWhiteout:
        if (!RecordValueWhiteout(relative, value_name))
        {
            /* Failing closed: the value of the host would reappear otherwise. */
            return STATUS_UNSUCCESSFUL;
        }
        return STATUS_SUCCESS;
    }

    return status;
}

NTSTATUS appbox::registry::Hive::QueryMultipleValues(HANDLE KeyHandle, const std::wstring& view_path,
                                                     const std::wstring& relative, PKEY_VALUE_ENTRY ValueEntries,
                                                     ULONG EntryCount, PVOID ValueBuffer, PULONG BufferLength,
                                                     PULONG RequiredBufferLength)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    if (ValueEntries == nullptr || EntryCount == 0)
    {
        /* Nothing to classify: the kernel decides about the arguments. */
        return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    /*
     * Classify every entry: the hive layer wins, a value which only the host
     * holds is read through, and a value which the isolation hides is missing
     * in the view.
     */
    std::vector<std::wstring> names(EntryCount);
    std::vector<bool>         from_hive(EntryCount, false);
    std::vector<bool>         from_host(EntryCount, false);
    bool                      any_hive = false;
    bool                      any_host = false;

    for (ULONG index = 0; index < EntryCount; ++index)
    {
        if (!ReadValueName(ValueEntries[index].ValueName, names[index]))
        {
            /* The name cannot be read without touching memory the caller does
             * not own; the kernel rejects such a call, so it is forwarded. */
            return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                               RequiredBufferLength);
        }

        if (HiveHoldsValue(KeyHandle, names[index]))
        {
            from_hive[index] = true;
            any_hive = true;
            continue;
        }

        if (IsHiddenRealEntry(relative, true, names[index]) || !HostHoldsValue(view_path, names[index]))
        {
            /* A value which is missing in the view fails the whole batch, which
             * is what the kernel reports for a batch which names a value the
             * key does not hold. */
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        from_host[index] = true;
        any_host = true;
    }

    if (!any_host)
    {
        /* The whole batch lives in the hive, the kernel answers it. */
        return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    HANDLE real = nullptr;
    if (!NT_SUCCESS(OpenRealKey(view_path, KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    appbox::registry::KeyGuard real_guard(real);

    if (!any_hive)
    {
        /* The whole batch lives in the host layer, the kernel answers it. */
        return sys_NtQueryMultipleValueKey(real, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    /*
     * The batch mixes both layers, so it is assembled here: every value is read
     * from the layer which holds it and the data is packed into the caller
     * buffer the way the kernel packs it (the entries follow each other without
     * padding).
     */
    std::vector<std::vector<BYTE>> datas(EntryCount);
    std::vector<ULONG>             types(EntryCount, REG_NONE);
    unsigned long long             total = 0;

    for (ULONG index = 0; index < EntryCount; ++index)
    {
        const HANDLE owner = from_hive[index] ? KeyHandle : real;
        const NTSTATUS status = ReadValueFull(owner, names[index], types[index], datas[index]);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        total += datas[index].size();
        if (total > 0xFFFFFFFFull)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
    }

    const ULONG required = static_cast<ULONG>(total);
    if (RequiredBufferLength != nullptr)
    {
        *RequiredBufferLength = required;
    }

    if (ValueBuffer == nullptr || BufferLength == nullptr || *BufferLength < required)
    {
        if (BufferLength != nullptr)
        {
            *BufferLength = 0;
        }
        /* The kernel reports the overflow as a warning status together with the
         * size the caller needs, which is what a caller checks. */
        return STATUS_BUFFER_OVERFLOW;
    }

    auto*      out = static_cast<BYTE*>(ValueBuffer);
    ULONG      offset = 0;
    for (ULONG index = 0; index < EntryCount; ++index)
    {
        if (!datas[index].empty())
        {
            memcpy(out + offset, datas[index].data(), datas[index].size());
        }

        ValueEntries[index].DataOffset = offset;
        ValueEntries[index].DataLength = static_cast<ULONG>(datas[index].size());
        ValueEntries[index].Type = types[index];
        offset += static_cast<ULONG>(datas[index].size());
    }

    *BufferLength = required;
    return STATUS_SUCCESS;
}

NTSTATUS appbox::registry::Hive::SaveIsolatedKey(HANDLE KeyHandle, const std::wstring& view_path,
                                                 const std::wstring& relative, HANDLE FileHandle, ULONG Format,
                                                 bool extended)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    /*
     * The hive layer of the key: the handle of the caller when it is a hive
     * handle, otherwise a handle opened here. A key which only the host holds
     * has no hive layer at all.
     */
    HANDLE owned = nullptr;
    HANDLE hive_key = KeyHandle;
    if (hive_key == nullptr && TryOpenHiveKey(relative, KEY_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, owned))
    {
        hive_key = owned;
    }

    /*
     * The fast path: when the host does not hold the key at all, the hive layer
     * is the whole view and the key can be saved directly.
     */
    if (hive_key != nullptr && !HostHoldsKey(view_path))
    {
        const NTSTATUS status =
            extended ? sys_NtSaveKeyEx(hive_key, FileHandle, Format) : sys_NtSaveKey(hive_key, FileHandle);
        CloseHiveHandle(owned);
        return status;
    }

    /*
     * The merged view of the key is copied into a temporary hive next to the
     * sandbox hive, and that key is saved. A leftover of an earlier run is
     * removed first, so the mount always starts from an empty hive.
     */
    const std::wstring scratch = BuildSaveHivePath();
    DeleteSaveHiveFiles(scratch);

    LOG_D("save of {}: temporary hive {}", appbox::WideToUTF8(relative), appbox::WideToUTF8(scratch));

    HKEY       scratch_root = nullptr;
    const LONG loaded = s_hive_data->load_app_key(scratch.c_str(), &scratch_root, KEY_ALL_ACCESS, 0, 0);
    if (loaded != ERROR_SUCCESS)
    {
        LOG_E("failed to mount the temporary hive {} of a save: {}", appbox::WideToUTF8(scratch), loaded);
        CloseHiveHandle(owned);
        return STATUS_UNSUCCESSFUL;
    }

    HANDLE scratch_handle = reinterpret_cast<HANDLE>(scratch_root);

    /*
     * The root key of the snapshot carries the name of the saved key, so the
     * file describes the same key a direct save would describe.
     */
    HANDLE   snapshot = nullptr;
    NTSTATUS status = CreateHiveKeyRelative(scratch_handle, LastKeyComponent(relative), KEY_ALL_ACCESS,
                                            OBJ_CASE_INSENSITIVE, nullptr, nullptr, 0, nullptr,
                                            REG_OPTION_NON_VOLATILE, &snapshot, nullptr);
    if (NT_SUCCESS(status))
    {
        status = CopyMergedKey(hive_key, view_path, relative, snapshot, 0);
    }
    if (NT_SUCCESS(status))
    {
        BYTE  key_info[sizeof(KEY_FULL_INFORMATION) + 0x100] = {};
        ULONG key_info_len = 0;
        ULONG values = 0;
        if (NT_SUCCESS(sys_NtQueryKey(snapshot, KeyFullInformation, key_info, sizeof(key_info), &key_info_len)))
        {
            values = reinterpret_cast<KEY_FULL_INFORMATION*>(key_info)->Values;
        }

        status = extended ? sys_NtSaveKeyEx(snapshot, FileHandle, Format) : sys_NtSaveKey(snapshot, FileHandle);
        LOG_I("save of {}: {} values exported, write {:#x}", appbox::WideToUTF8(relative), values, status);
    }

    CloseHiveHandle(snapshot);

    /* Closing the root handle unmounts the temporary hive and flushes it. */
    sys_NtClose(scratch_handle);
    DeleteSaveHiveFiles(scratch);
    CloseHiveHandle(owned);

    if (!NT_SUCCESS(status))
    {
        LOG_W("the save snapshot of {} failed: {:#x}", appbox::WideToUTF8(relative), status);
    }
    return status;
}
