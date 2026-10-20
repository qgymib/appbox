#include "utils/WinAPI.h" /* Must be first include file */
#include <fstream>
#include <vector>
#include "hook/NtCreateKey.hpp"
#include "hook/NtCreateKeyTransacted.hpp"
#include "hook/NtDeleteKey.hpp"
#include "hook/NtDeleteValueKey.hpp"
#include "hook/NtEnumerateKey.hpp"
#include "hook/NtEnumerateValueKey.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtOpenKey.hpp"
#include "hook/NtOpenKeyEx.hpp"
#include "hook/NtOpenKeyTransacted.hpp"
#include "hook/NtOpenKeyTransactedEx.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtQueryKey.hpp"
#include "hook/NtQueryMultipleValueKey.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryValueKey.hpp"
#include "hook/NtRenameKey.hpp"
#include "hook/NtSaveKey.hpp"
#include "hook/NtSaveKeyEx.hpp"
#include "hook/NtSetValueKey.hpp"
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
    /* [IN] */ LPCWSTR lpFile,
    /* [OUT] */ PHKEY  phKey,
    /* [IN] */ REGSAM  samDesired,
    /* [IN] */ DWORD   dwFlags,
    /* [IN] */ DWORD   dwReserved);

/**
 * @brief Opens the access token of a process.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntopenprocesstoken
 */
typedef NTSTATUS(NTAPI* T_NtOpenProcessToken)(
    /* [IN] */ HANDLE      ProcessHandle,
    /* [IN] */ ACCESS_MASK DesiredAccess,
    /* [OUT] */ PHANDLE    TokenHandle);

/**
 * @brief Queries the access token of a process.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationtoken
 */
typedef NTSTATUS(NTAPI* T_NtQueryInformationToken)(
    /* [IN] */ HANDLE      Handle,
    /* [IN] */ ULONG       TokenInformationClass,
    /* [OUT] */ PVOID      TokenInformation,
    /* [IN] */ ULONG       TokenInformationLength,
    /* [OUT,OPT] */ PULONG ReturnLength);

/**
 * @brief Converts a SID into its string representation.
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlconvertsidtounicodestring
 */
typedef NTSTATUS(NTAPI* T_RtlConvertSidToUnicodeString)(
    /* [OUT] */ PUNICODE_STRING UnicodeString,
    /* [IN] */ PVOID            Sid,
    /* [IN] */ BOOLEAN          AllocateDestinationString);

/**
 * @brief Frees a buffer which was allocated by RtlConvertSidToUnicodeString().
 */
typedef void(NTAPI* T_RtlFreeUnicodeStringLocal)(PUNICODE_STRING UnicodeString);

/**
 * @brief The kernel object name of the root of every application hive.
 */
static const wchar_t* s_app_hive_root_name = L"\\REGISTRY\\A";

/**
 * @brief Runtime state of the registry hive module.
 */
struct appbox::registry::Hive::Data
{
    HANDLE         hive_root = nullptr; /* Root key handle of the state hive mount. */
    std::wstring   hive_mount_name;     /* Object name of the mount, for example \REGISTRY\A\{GUID}. */
    std::wstring   hkcu_prefix;         /* NT path of the real HKCU root, \REGISTRY\USER\<SID>. */
    std::wstring   hive_path;           /* DOS path of the state hive file. */
    IsolationTable isolation;           /* Isolation modes of the virtual registry. */

    /**
     * @brief Root key handle of the read-only image of the run.
     *
     * The image carries the registry of the archive merged with the hives of
     * the patch packages, in the order the packages take effect in. It is the
     * layer below the state hive and above the real registry, and it is never
     * written by the sandbox: a modification of an entry the image holds is
     * copied up into the state hive. Null when the run has no image.
     */
    HANDLE image_root = nullptr;

    /**
     * @brief Object name of the image mount, for example \REGISTRY\A\{GUID}.
     */
    std::wstring image_mount_name;

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

    auto fn = reinterpret_cast<NTSTATUS(NTAPI*)(HANDLE)>(GetProcAddress(ntdll, "NtClose"));
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
    auto fn_query_token = reinterpret_cast<T_NtQueryInformationToken>(GetProcAddress(ntdll, "NtQueryInformationToken"));
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
        us.Buffer = const_cast<PWSTR>(root_name.c_str());
        us.Length = static_cast<USHORT>(root_name.size() * sizeof(wchar_t));
        us.MaximumLength = static_cast<USHORT>(us.Length + sizeof(wchar_t));
        InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, hive_root, nullptr);

        HANDLE   key = nullptr;
        ULONG    disposition = 0;
        NTSTATUS status = fn_create_key(&key, KEY_ALL_ACCESS, &oa, 0, nullptr, REG_OPTION_NON_VOLATILE, &disposition);
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
 * The files are read in the order of the layers of the run: the file of the
 * resources of the archive first, then the file of every patch package in
 * ascending order. Every file overrides the modes the files below it set for
 * the same key or value, so the sandboxed process observes the mode of the
 * last layer which names an entry.
 *
 * The isolation file is the description of the run: the sandbox cannot tell
 * which host entries stay visible without it, and there is no default mode to
 * fall back to, so the file of the archive is required and a file which cannot
 * be read, which is malformed or which carries another version fails the start
 * of the sandbox. A patch package which carries no file changes nothing, while
 * a file a package carries has to be usable as well: the modes of the layers
 * below it would otherwise stay in place silently.
 *
 * @param[in,out] data The runtime state which receives the modes.
 * @param[out] error Description of the failure.
 * @return true when the modes of every layer were applied.
 */
static bool LoadIsolationTable(appbox::registry::Hive::Data& data, std::string& error)
{
    error.clear();

    const auto& paths = appbox::sandbox->wRegistryIsolationDOSPaths;
    if (paths.empty())
    {
        error = "no registry isolation file is configured";
        return false;
    }

    for (std::size_t index = 0; index < paths.size(); ++index)
    {
        /* The launcher lists the file of the archive first, the files of the
         * patch packages after it. */
        const bool  archive_layer = index == 0;
        const auto& path = paths[index];

        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            if (archive_layer)
            {
                error = "the registry isolation file of the archive does not exist: " + appbox::WideToUTF8(path);
                return false;
            }

            LOG_D("a patch package carries no registry isolation file: {}", appbox::WideToUTF8(path));
            continue;
        }

        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::string       detail;
        if (!data.isolation.Parse(text, detail))
        {
            error = "the registry isolation file '" + appbox::WideToUTF8(path) + "' cannot be used: " + detail;
            return false;
        }
    }

    /*
     * Without a mode for every root key of the view the mode of a path cannot
     * resolve, which would leave the host registry visible for the entries the
     * table does not cover.
     */
    if (!data.isolation.NamesRootKeys())
    {
        error = "the registry isolation file names no mode for every root key of the view";
        return false;
    }

    LOG_I("registry isolation modes loaded: {} keys, {} values", data.isolation.KeyCount(),
          data.isolation.ValueCount());
    return true;
}

/**
 * @brief Create or open a key relative to a parent handle of the hive.
 * @see CreateHiveKeyRelative()
 */
static NTSTATUS CreateHiveKeyRelative(HANDLE parent, const std::wstring& name, ACCESS_MASK DesiredAccess,
                                      ULONG Attributes, PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                      ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions,
                                      HANDLE TransactionHandle, PHANDLE KeyHandle, PULONG Disposition);

/**
 * @brief Create or open a key of the hive, the transaction handle included.
 * @see CreateHiveKey()
 */
static NTSTATUS CreateHiveKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                              PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex,
                              PUNICODE_STRING Class, ULONG CreateOptions, HANDLE TransactionHandle, PHANDLE KeyHandle,
                              PULONG Disposition);

/**
 * @brief Open a key of the state hive when the hive holds it.
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
 * @brief Open a key of the read-only image when the image holds it.
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] access The requested access mask.
 * @param[out] key The handle of the key, null when the image does not hold it.
 * @return true when the key was opened.
 */
static bool TryOpenImageKey(const std::wstring& relative, ACCESS_MASK access, HANDLE& key)
{
    key = nullptr;
    return NT_SUCCESS(appbox::registry::Hive::OpenImageKey(relative, access, &key));
}

/**
 * @brief Whether a layer of the sandbox holds a key.
 * @param[in] image true tests the image, false the state hive.
 * @param[in] relative The key path relative to the hive root.
 * @return true when the layer holds the key.
 */
static bool LayerHoldsKey(bool image, const std::wstring& relative)
{
    HANDLE key = nullptr;
    if (!(image ? TryOpenImageKey(relative, KEY_QUERY_VALUE, key) : TryOpenHiveKey(relative, KEY_QUERY_VALUE, key)))
    {
        return false;
    }

    CloseHiveHandle(key);
    return true;
}

/**
 * @brief Whether a layer of the sandbox holds a value of a key.
 * @param[in] image true tests the image, false the state hive.
 * @param[in] relative The key path relative to the hive root.
 * @param[in] value_name Name of the value, empty for the default value.
 * @return true when the layer holds the value.
 */
static bool LayerHoldsValue(bool image, const std::wstring& relative, const std::wstring& value_name);

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

    BYTE           buffer[sizeof(KEY_VALUE_BASIC_INFORMATION) + 0x100] = {};
    ULONG          result = 0;
    const NTSTATUS status = sys_NtQueryValueKey(key, &us, KeyValueBasicInformation, buffer, sizeof(buffer), &result);
    return NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL;
}

static bool LayerHoldsValue(bool image, const std::wstring& relative, const std::wstring& value_name)
{
    HANDLE key = nullptr;
    if (!(image ? TryOpenImageKey(relative, KEY_QUERY_VALUE, key) : TryOpenHiveKey(relative, KEY_QUERY_VALUE, key)))
    {
        return false;
    }

    appbox::registry::KeyGuard guard(key);
    return HiveHoldsValue(key, value_name);
}

/**
 * @brief Whether the granted access of a key handle permits a value write.
 *
 * A call the isolation re-routes to the other layer runs against a key the
 * isolation opened, so the kernel cannot check the rights of the handle of the
 * caller any more. The access mask the kernel granted to that handle is queried
 * and tested instead, so a caller which opened the key without the right to
 * write does not gain it through the isolation.
 *
 * @param[in] KeyHandle The key handle of the caller.
 * @return true when the handle permits a write of a value.
 */
static bool HandlePermitsValueWrite(HANDLE KeyHandle)
{
    BYTE  buffer[sizeof(PUBLIC_OBJECT_BASIC_INFORMATION)] = {};
    ULONG result = 0;
    if (sys_NtQueryObject == nullptr ||
        !NT_SUCCESS(sys_NtQueryObject(KeyHandle, ObjectBasicInformation, buffer, sizeof(buffer), &result)))
    {
        return false;
    }

    const auto* info = reinterpret_cast<const PUBLIC_OBJECT_BASIC_INFORMATION*>(buffer);
    return (info->GrantedAccess & (KEY_SET_VALUE | GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED)) != 0;
}

/**
 * @brief Whether the granted access of a key handle permits a delete.
 *
 * The counterpart of `HandlePermitsValueWrite()` for the calls which remove the
 * key itself: the isolation re-routes a delete of a key of the read-only image
 * to the state hive, so the rights of the handle of the caller have to be
 * tested before the call runs against a handle the isolation opened.
 *
 * @param[in] KeyHandle The key handle of the caller.
 * @return true when the handle permits the delete of the key.
 */
static bool HandlePermitsKeyDelete(HANDLE KeyHandle)
{
    BYTE  buffer[sizeof(PUBLIC_OBJECT_BASIC_INFORMATION)] = {};
    ULONG result = 0;
    if (sys_NtQueryObject == nullptr ||
        !NT_SUCCESS(sys_NtQueryObject(KeyHandle, ObjectBasicInformation, buffer, sizeof(buffer), &result)))
    {
        return false;
    }

    const auto* info = reinterpret_cast<const PUBLIC_OBJECT_BASIC_INFORMATION*>(buffer);
    return (info->GrantedAccess & (DELETE | GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED)) != 0;
}

/**
 * @brief Create or open a key of the real registry through its view path.
 *
 * The helper is the host counterpart of `CreateHiveKey` for the write rule of
 * `Merge`: a modification of an entry the sandbox does not hold lands in the real
 * registry, and the key which has to carry it is created when the host does not
 * hold it yet.
 *
 * `NtCreateKey` creates a key only when the key above it exists, so a path whose
 * parents are missing is walked from its first component: the helper creates the
 * parent of the key first and creates the key below the handle of that parent.
 * The walk ends at a root key of the view, which the real registry always holds,
 * so the recursion is bounded by the length of the path.
 *
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] DesiredAccess The access mask the caller needs on the key.
 * @param[out] key The handle of the key, null on failure.
 * @return Status code.
 */
static NTSTATUS CreateRealKey(const std::wstring& view_path, ACCESS_MASK DesiredAccess, HANDLE& key)
{
    key = nullptr;

    /* A key the real registry holds is opened with the access of the caller. */
    const NTSTATUS opened =
        appbox::registry::Hive::OpenRealKey(view_path, DesiredAccess, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &key);
    if (NT_SUCCESS(opened) || !appbox::registry::IsKeyNotFound(opened))
    {
        return opened;
    }

    const std::size_t separator = view_path.rfind(L'\\');
    if (separator == std::wstring::npos || separator == 0)
    {
        /* The root key of the view is not part of the hive, so a path which ends
         * here names a key the real registry does not hold. */
        return opened;
    }

    /* The key does not exist, so the key above it is created first. */
    HANDLE         parent = nullptr;
    const NTSTATUS created =
        CreateRealKey(view_path.substr(0, separator), KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE, parent);
    if (!NT_SUCCESS(created))
    {
        return created;
    }

    appbox::registry::KeyGuard guard(parent);

    const std::wstring name = view_path.substr(separator + 1);

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, name.c_str());
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, parent, nullptr);

    ULONG disposition = 0;
    return sys_NtCreateKey(&key, DesiredAccess, &oa, 0, nullptr, REG_OPTION_NON_VOLATILE, &disposition);
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
        return appbox::registry::Hive::HidesHostValue(relative, name) ||
               appbox::registry::Hive::IsValueWhitedOut(relative, name);
    }

    const std::wstring child = appbox::registry::JoinKeyPath(relative, name);
    return appbox::registry::KeyHidesHost(appbox::registry::Hive::KeyIsolation(child)) ||
           appbox::registry::Hive::IsKeyWhitedOut(child);
}

/**
 * @brief Whether an entry of a sandbox layer below the state hive is hidden.
 *
 * The state of the sandbox is the highest layer of the view, so an entry it
 * deleted stays invisible even when the image of the run still carries it. The
 * isolation mode does not take part in the filter: `Full` and `Hide` keep the
 * host registry invisible and never the sandbox layers, and the packer already
 * moved the content of a `Hide` entry into the hidden store of its hive.
 *
 * @param[in] relative The hive relative path of the parent key.
 * @param[in] values true tests a value name, false a sub key name.
 * @param[in] name Name of the entry inside the layer.
 * @return true when the entry must not be part of the merged view.
 */
static bool IsHiddenLowerEntry(const std::wstring& relative, bool values, const std::wstring& name)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    if (appbox::registry::Hive::IsKeyWhitedOut(relative))
    {
        /* The key itself was deleted: its whole subtree is gone. */
        return true;
    }

    if (values)
    {
        return appbox::registry::Hive::IsValueWhitedOut(relative, name);
    }

    return appbox::registry::Hive::IsKeyWhitedOut(appbox::registry::JoinKeyPath(relative, name));
}

/**
 * @brief Drop the names of a sandbox layer below the state hive which a
 *        whiteout hides.
 *
 * @param[in] relative The hive relative path of the parent key.
 * @param[in] values true filters value names, false filters sub key names.
 * @param[in,out] names The names of the layer, filtered in place.
 */
static void FilterHiddenLowerEntries(const std::wstring& relative, bool values, std::vector<std::wstring>& names)
{
    if (s_hive_data == nullptr || !s_hive_data->whiteout_possible)
    {
        return;
    }

    std::vector<std::wstring> visible;
    visible.reserve(names.size());
    for (const auto& name : names)
    {
        if (!IsHiddenLowerEntry(relative, values, name))
        {
            visible.push_back(name);
        }
    }
    names.swap(visible);
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
    std::wstring                    name;   /* Name of the entry inside its layer. */
    HANDLE                          handle; /* Handle which holds the entry. */
    bool                            owned;  /* The caller has to close the handle. */
    appbox::registry::MergedResolve layer;  /* The layer which holds the entry. */
};

/**
 * @brief Resolve one entry of the merged view of a key.
 *
 * The entry is looked up through the merged index of the view (see
 * `Hive::ResolveMergedIndex`), so the snapshot copies exactly the entries the
 * sandboxed process sees.
 *
 * @param[in] hive_key The state hive layer handle of the key, may be null.
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] values true resolves a value, false a sub key.
 * @param[in] index The index inside the merged view.
 * @param[out] entry The resolved entry. An entry of the image or of the real
 *                   layer owns its handle and the caller has to close it.
 * @return Status code, STATUS_NO_MORE_ENTRIES past the end of the view.
 */
static NTSTATUS ResolveMergedEntry(HANDLE hive_key, const std::wstring& view_path, bool values, ULONG index,
                                   MergedEntry& entry)
{
    HANDLE layer_handle = nullptr;
    bool   owned = false;
    ULONG  layer_index = 0;

    const appbox::registry::MergedResolve resolve = appbox::registry::Hive::ResolveMergedIndex(
        hive_key, view_path, values, index, layer_handle, owned, layer_index);
    if (resolve == appbox::registry::MergedResolve::NoMoreEntries)
    {
        return STATUS_NO_MORE_ENTRIES;
    }
    if (resolve == appbox::registry::MergedResolve::Error)
    {
        return STATUS_UNSUCCESSFUL;
    }

    entry.layer = resolve;
    entry.handle = layer_handle;
    entry.owned = owned;

    std::vector<std::wstring> names;
    const NTSTATUS            status = values ? appbox::registry::Hive::CollectValueNames(entry.handle, names)
                                              : appbox::registry::Hive::CollectSubKeyNames(entry.handle, names);
    if (!NT_SUCCESS(status) || layer_index >= names.size())
    {
        if (entry.owned)
        {
            sys_NtClose(entry.handle);
        }
        entry.handle = nullptr;
        entry.owned = false;
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
        NTSTATUS    status = ResolveMergedEntry(hive_key, view_path, true, index, entry);
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
        if (entry.owned)
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
        NTSTATUS    status = ResolveMergedEntry(hive_key, view_path, false, index, entry);
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

        LOG_D("snapshot of {}: sub key {} (layer {})", appbox::WideToUTF8(relative), appbox::WideToUTF8(entry.name),
              static_cast<int>(entry.layer));

        /*
         * A child of the state hive is opened from that layer, a child which
         * only the image or the real layer holds has no state hive key at all:
         * the merge answered it from that layer because no layer above it
         * shadows the name.
         */
        HANDLE child_hive = nullptr;
        if (entry.layer == appbox::registry::MergedResolve::HiveLayer)
        {
            if (!TryOpenHiveKey(child_relative, KEY_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, child_hive))
            {
                /* The state hive holds the child, so the snapshot would be
                 * incomplete. */
                return STATUS_UNSUCCESSFUL;
            }
        }
        else if (entry.owned)
        {
            sys_NtClose(entry.handle);
        }

        HANDLE   child_destination = nullptr;
        NTSTATUS create_status =
            CreateHiveKeyRelative(destination, entry.name, KEY_ALL_ACCESS, OBJ_CASE_INSENSITIVE, nullptr, nullptr, 0,
                                  nullptr, REG_OPTION_NON_VOLATILE, nullptr, &child_destination, nullptr);
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
     * exist yet, so no template is needed. The mount is not part of the global
     * registry namespace and is reached only through the returned root handle.
     * A file which another process of the sandbox already mounted is served by
     * the hive of that mount, so the processes of one sandbox share the
     * registry state of the run.
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

    /*
     * Mount the read-only image of the run: it carries the registry of the
     * archive merged with the hives of the patch packages, and it is the layer
     * between the state hive and the real registry. A run whose image cannot be
     * mounted keeps the state hive and the real registry: the content of the
     * archive is missing, which is reported, but the isolation keeps working.
     */
    if (!appbox::sandbox->wRegistryImageHiveDOSPath.empty())
    {
        HKEY image_root = nullptr;
        err = fn_load_app_key(appbox::sandbox->wRegistryImageHiveDOSPath.c_str(), &image_root, KEY_ALL_ACCESS, 0, 0);
        if (err != ERROR_SUCCESS)
        {
            LOG_W("RegLoadAppKeyW({}) failed: {}: the view carries the state of the sandbox alone",
                  appbox::WideToUTF8(appbox::sandbox->wRegistryImageHiveDOSPath), err);
        }
        else
        {
            data->image_root = reinterpret_cast<HANDLE>(image_root);
            if (!QueryObjectName(data->image_root, data->image_mount_name))
            {
                LOG_W("failed to query the object name of the image root: the image is not part of the view");
                CloseLocal(data->image_root);
                data->image_root = nullptr;
                data->image_mount_name.clear();
            }
        }
    }

    /* The five root keys of the view, so every root open lands in a hive. */
    NTSTATUS status = CreateRootKeys(data->hive_root);
    if (NT_SUCCESS(status) && data->image_root != nullptr)
    {
        status = CreateRootKeys(data->image_root);
    }
    if (!NT_SUCCESS(status))
    {
        if (data->image_root != nullptr)
        {
            CloseLocal(data->image_root);
            data->image_root = nullptr;
        }
        CloseLocal(data->hive_root);
        delete data;
        return status;
    }

    /*
     * The modes of the virtual registry, which decide which host entries stay
     * invisible. Without them the isolation cannot answer a single call, so a
     * failure of the load fails the mount of the hive.
     */
    std::string isolation_error;
    if (!LoadIsolationTable(*data, isolation_error))
    {
        LOG_E("the registry isolation modes cannot be loaded: {}", isolation_error);
        if (data->image_root != nullptr)
        {
            CloseLocal(data->image_root);
        }
        CloseLocal(data->hive_root);
        delete data;
        return STATUS_INVALID_PARAMETER;
    }

    s_hive_data = data;

    /*
     * A whiteout store which does not exist when the hive is mounted stays
     * empty for the whole run, because the sandbox is its only writer. The
     * flag keeps a sandbox which never deleted anything free of the lookups.
     */
    data->whiteout_possible = WhiteoutStoreExists();

    LOG_I("registry hive mounted: {} (mount: {}, image: {}, hkcu: {}, whiteout store: {})",
          appbox::WideToUTF8(data->hive_path), appbox::WideToUTF8(data->hive_mount_name),
          data->image_root == nullptr ? std::string("none") : appbox::WideToUTF8(data->image_mount_name),
          appbox::WideToUTF8(data->hkcu_prefix), data->whiteout_possible);
    return STATUS_SUCCESS;
}

void appbox::registry::Hive::Exit()
{
    if (s_hive_data != nullptr)
    {
        if (s_hive_data->image_root != nullptr)
        {
            CloseLocal(s_hive_data->image_root);
        }
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

appbox::RegistryKeyIsolation appbox::registry::Hive::KeyIsolation(const std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        /* Without a table the host registry must stay invisible. */
        return RegistryKeyIsolation::Hide;
    }

    return s_hive_data->isolation.KeyMode(relative);
}

std::optional<appbox::RegistryValueIsolation> appbox::registry::Hive::ListedValueIsolation(
    const std::wstring& relative, const std::wstring& value_name)
{
    if (s_hive_data == nullptr)
    {
        return std::nullopt;
    }

    return s_hive_data->isolation.ListedValueMode(relative, value_name);
}

bool appbox::registry::Hive::HidesHostValue(const std::wstring& relative, const std::wstring& value_name)
{
    return appbox::registry::ValueHidesHost(ListedValueIsolation(relative, value_name), KeyIsolation(relative));
}

bool appbox::registry::Hive::ValueWritesToHost(const std::wstring& relative, const std::wstring& value_name)
{
    return appbox::registry::ValueWritesToHost(ListedValueIsolation(relative, value_name), KeyIsolation(relative),
                                               SandboxHoldsValue(relative, value_name));
}

bool appbox::registry::Hive::SandboxHoldsValue(const std::wstring& relative, const std::wstring& value_name)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    /*
     * A value the sandbox deleted counts as held: the deletion is recorded in
     * the whiteout store, so a later write of the value has to land in the
     * sandbox again.
     */
    if (IsValueWhitedOut(relative, value_name))
    {
        return true;
    }

    return LayerHoldsValue(false, relative, value_name) || LayerHoldsValue(true, relative, value_name);
}

bool appbox::registry::Hive::SandboxHoldsKey(const std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return false;
    }

    /* A key the sandbox deleted counts as held, like a value it deleted. */
    if (IsKeyWhitedOut(relative))
    {
        return true;
    }

    return LayerHoldsKey(false, relative) || LayerHoldsKey(true, relative);
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

/**
 * @brief Open a key of the hive, the transaction handle included (NtOpenKey semantics).
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] TransactionHandle The transaction which receives the key, null
 *                              for a plain open. The key is opened with the
 *                              transacted entry point when it is not null, so
 *                              the handle the caller receives is bound to the
 *                              transaction.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenHiveKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                            PVOID SecurityDescriptor, PVOID SecurityQualityOfService, HANDLE TransactionHandle,
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

    if (TransactionHandle != nullptr)
    {
        return sys_NtOpenKeyTransacted(KeyHandle, DesiredAccess, &oa, TransactionHandle);
    }

    return sys_NtOpenKey(KeyHandle, DesiredAccess, &oa);
}

/**
 * @brief Open a key of the hive, the transaction handle included (NtOpenKeyEx semantics).
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] OpenOptions The open options of the original call.
 * @param[in] TransactionHandle The transaction which receives the key, null for
 *                              a plain open.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenHiveKeyEx(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                              PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                              HANDLE TransactionHandle, PHANDLE KeyHandle)
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

    if (TransactionHandle != nullptr)
    {
        return sys_NtOpenKeyTransactedEx(KeyHandle, DesiredAccess, &oa, OpenOptions, TransactionHandle);
    }

    return sys_NtOpenKeyEx(KeyHandle, DesiredAccess, &oa, OpenOptions);
}

NTSTATUS appbox::registry::Hive::OpenKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                         PVOID SecurityDescriptor, PVOID SecurityQualityOfService, PHANDLE KeyHandle)
{
    return OpenHiveKey(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService, nullptr,
                       KeyHandle);
}

NTSTATUS appbox::registry::Hive::OpenKeyEx(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                           PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                                           PHANDLE KeyHandle)
{
    return OpenHiveKeyEx(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService, OpenOptions,
                         nullptr, KeyHandle);
}

NTSTATUS appbox::registry::Hive::OpenImageKey(const std::wstring& relative, ACCESS_MASK DesiredAccess,
                                              PHANDLE KeyHandle)
{
    if (s_hive_data == nullptr || s_hive_data->image_root == nullptr)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, relative.c_str());
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, s_hive_data->image_root, nullptr);
    return sys_NtOpenKey(KeyHandle, DesiredAccess, &oa);
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
                                               PVOID SecurityQualityOfService, ULONG OpenOptions, PHANDLE KeyHandle)
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
 * @param[in] TransactionHandle The transaction of the caller, null for an open
 *                              which is not part of a transaction.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenHiveKeyEntry(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                 PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                                 bool extended, HANDLE TransactionHandle, PHANDLE KeyHandle)
{
    if (extended)
    {
        return OpenHiveKeyEx(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService,
                             OpenOptions, TransactionHandle, KeyHandle);
    }

    return OpenHiveKey(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService,
                       TransactionHandle, KeyHandle);
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
 * A call which carries a transaction of the caller is answered by the same
 * policy with two differences: the key of the hive is opened with that
 * transaction, so the handle of the caller is bound to it, and the host layer
 * is never used — a read through would enlist the real hive into the
 * transaction of the caller, which is why the table refuses it (see
 * `appbox::registry::FallbackForKeyTransacted`).
 *
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] OpenOptions The open options of the original call.
 * @param[in] extended true runs the extended entry points, false the plain ones.
 * @param[in] TransactionHandle The transaction of the caller, null for an open
 *                              which is not part of a transaction.
 * @param[out] KeyHandle The resulting key handle.
 * @return Status code.
 */
static NTSTATUS OpenIsolatedKeyEntry(const std::wstring& view_path, const std::wstring& relative,
                                     ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                     PVOID SecurityQualityOfService, ULONG OpenOptions, bool extended,
                                     HANDLE TransactionHandle, PHANDLE KeyHandle)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    const appbox::RegistryKeyIsolation mode = appbox::registry::Hive::KeyIsolation(relative);

    HANDLE   key = nullptr;
    NTSTATUS st_hive = OpenHiveKeyEntry(relative, DesiredAccess, Attributes, SecurityDescriptor,
                                        SecurityQualityOfService, OpenOptions, extended, TransactionHandle, &key);
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

    /*
     * The image of the run is the layer below the state hive, and a key it holds
     * is part of the view of every mode: `Full` and `Hide` keep the host
     * registry invisible and never a layer of the sandbox. A read hands the
     * handle of the image out, while a write copies the key up into the state
     * hive, because the image is never written.
     *
     * A transacted call never uses the image: the handle of that layer is not
     * bound to the transaction of the caller, which is the rule the host layer
     * follows as well.
     */
    if (TransactionHandle == nullptr)
    {
        const bool        write_access = appbox::registry::RequestsWrite(DesiredAccess);
        const ACCESS_MASK probe = write_access ? (KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS) : DesiredAccess;

        HANDLE image = nullptr;
        if (TryOpenImageKey(relative, probe, image))
        {
            if (!write_access)
            {
                *KeyHandle = image;
                return STATUS_SUCCESS;
            }

            CloseHiveHandle(image);

            const NTSTATUS st_copy =
                CreateHiveKey(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService, 0,
                              nullptr, REG_OPTION_NON_VOLATILE, nullptr, KeyHandle, nullptr);
            if (!NT_SUCCESS(st_copy))
            {
                /* Failing closed: the caller asked for a write and the image
                 * must never receive one. */
                LOG_E("failed to copy the key {} of the image up into the state hive: {:#x}",
                      appbox::WideToUTF8(relative), st_copy);
            }
            return st_copy;
        }
    }

    /*
     * The route of the fallback, chosen by the table of the call: the
     * transacted table never sends an open to the host layer, so its refusal is
     * translated into a return here and its copy-up into the branch below.
     */
    appbox::registry::OpenFallback route = appbox::registry::OpenFallback::ReportHiveFailure;
    if (TransactionHandle != nullptr)
    {
        switch (appbox::registry::FallbackForKeyTransacted(mode, DesiredAccess))
        {
        case appbox::registry::TransactedFallback::ReportHiveFailure:
            return st_hive;

        case appbox::registry::TransactedFallback::RefuseHostFallback:
            /*
             * The hive layer did not answer the open and the only handle the
             * isolation could hand out for the key is a handle of the host
             * layer, which would enlist the real hive into the transaction of
             * the caller. Failing closed keeps the real registry untouched.
             */
            LOG_W("the hive layer did not answer the transacted open of {} and the isolation never enlists the host "
                  "layer into a transaction: {:#x}",
                  appbox::WideToUTF8(relative), appbox::registry::kTransactedHostFallbackStatus);
            return appbox::registry::kTransactedHostFallbackStatus;

        case appbox::registry::TransactedFallback::CopyUp:
            route = appbox::registry::OpenFallback::CopyUp;
            break;
        }
    }
    else
    {
        route = appbox::registry::FallbackForKey(mode, DesiredAccess);
    }

    switch (route)
    {
    case appbox::registry::OpenFallback::ReportHiveFailure:
        /* `Full` and `Hide`: the host entry does not exist for the sandbox. */
        return st_hive;

    case appbox::registry::OpenFallback::UseHost:
        return OpenRealKeyEntry(view_path, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService,
                                OpenOptions, extended, KeyHandle);

    case appbox::registry::OpenFallback::CopyUp: {
        /*
         * `WriteCopy` with write access: the caller would modify the host key
         * through a real handle, so the key is copied up instead. The shadow
         * key takes the place of the real key — the merged enumeration, the
         * read through of the values and the merged counts keep the entries of
         * the real key visible.
         *
         * The probe of the host layer is a plain open without the transaction:
         * the host layer is never enlisted into the transaction of the caller,
         * while the shadow key is created with it, so a rollback removes the
         * shadow key again.
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

        NTSTATUS st_copy =
            CreateHiveKey(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService, 0, nullptr,
                          REG_OPTION_NON_VOLATILE, TransactionHandle, KeyHandle, nullptr);
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
                                                 ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                                 PVOID SecurityQualityOfService, PHANDLE KeyHandle)
{
    return OpenIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                SecurityQualityOfService, 0, false, nullptr, KeyHandle);
}

NTSTATUS appbox::registry::Hive::OpenIsolatedKeyEx(const std::wstring& view_path, const std::wstring& relative,
                                                   ACCESS_MASK DesiredAccess, ULONG Attributes,
                                                   PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                                   ULONG OpenOptions, PHANDLE KeyHandle)
{
    return OpenIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                SecurityQualityOfService, OpenOptions, true, nullptr, KeyHandle);
}

NTSTATUS appbox::registry::Hive::OpenIsolatedKeyTransacted(const std::wstring& view_path, const std::wstring& relative,
                                                           ACCESS_MASK DesiredAccess, ULONG Attributes,
                                                           PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                                           HANDLE TransactionHandle, PHANDLE KeyHandle)
{
    return OpenIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                SecurityQualityOfService, 0, false, TransactionHandle, KeyHandle);
}

NTSTATUS appbox::registry::Hive::OpenIsolatedKeyTransactedEx(const std::wstring& view_path,
                                                             const std::wstring& relative, ACCESS_MASK DesiredAccess,
                                                             ULONG Attributes, PVOID SecurityDescriptor,
                                                             PVOID SecurityQualityOfService, ULONG OpenOptions,
                                                             HANDLE TransactionHandle, PHANDLE KeyHandle)
{
    return OpenIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                SecurityQualityOfService, OpenOptions, true, TransactionHandle, KeyHandle);
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
 * @param[in] TransactionHandle The transaction which receives the key, null
 *                              for a plain create. The key is created with the
 *                              transacted entry point when it is not null, so
 *                              a rollback removes it again.
 * @param[out] KeyHandle The resulting key handle.
 * @param[out] Disposition REG_CREATED_NEW_KEY or REG_OPENED_EXISTING_KEY, may be null.
 * @return Status code.
 */
static NTSTATUS CreateHiveKeyRelative(HANDLE parent, const std::wstring& name, ACCESS_MASK DesiredAccess,
                                      ULONG Attributes, PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                      ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions,
                                      HANDLE TransactionHandle, PHANDLE KeyHandle, PULONG Disposition)
{
    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, name.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, parent, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    if (TransactionHandle != nullptr)
    {
        return sys_NtCreateKeyTransacted(KeyHandle, DesiredAccess, &oa, TitleIndex, Class, CreateOptions,
                                         TransactionHandle, Disposition);
    }

    return sys_NtCreateKey(KeyHandle, DesiredAccess, &oa, TitleIndex, Class, CreateOptions, Disposition);
}

/**
 * @brief Create or open a key of the hive, the transaction handle included.
 *
 * The path is walked component by component, which is what makes a create of a
 * path the hive does not hold yet work. Every component is created with the
 * transaction of the caller when it is not null, so a rollback removes the
 * whole path the create added instead of leaving the intermediate keys behind.
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] TitleIndex The title index of the original call.
 * @param[in] Class The key class of the original call.
 * @param[in] CreateOptions The create options of the original call.
 * @param[in] TransactionHandle The transaction of the caller, null for a
 *                              create which is not part of a transaction.
 * @param[out] KeyHandle The resulting key handle.
 * @param[out] Disposition REG_CREATED_NEW_KEY or REG_OPENED_EXISTING_KEY, may be null.
 * @return Status code.
 */
static NTSTATUS CreateHiveKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                              PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex,
                              PUNICODE_STRING Class, ULONG CreateOptions, HANDLE TransactionHandle, PHANDLE KeyHandle,
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
                                                REG_OPTION_NON_VOLATILE, TransactionHandle, &child, nullptr);
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
                                            CreateOptions, TransactionHandle, KeyHandle, Disposition);

    for (const HANDLE handle : intermediates)
    {
        CloseHiveHandle(handle);
    }

    return status;
}

NTSTATUS appbox::registry::Hive::CreateKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                           PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex,
                                           PUNICODE_STRING Class, ULONG CreateOptions, PHANDLE KeyHandle,
                                           PULONG Disposition)
{
    return CreateHiveKey(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService, TitleIndex,
                         Class, CreateOptions, nullptr, KeyHandle, Disposition);
}

/**
 * @brief Create or open an isolated key and report the disposition of the merged view.
 *
 * @param[in] view_path The logical path of the key in the view.
 * @param[in] relative The key path relative to the hive root.
 * @param[in] DesiredAccess The requested access mask.
 * @param[in] Attributes The object attributes flags of the original call.
 * @param[in] SecurityDescriptor The security descriptor of the original call.
 * @param[in] SecurityQualityOfService The quality of service of the original call.
 * @param[in] TitleIndex The title index of the original call.
 * @param[in] Class The key class of the original call.
 * @param[in] CreateOptions The create options of the original call.
 * @param[in] TransactionHandle The transaction of the caller, null for a
 *                              create which is not part of a transaction.
 * @param[out] KeyHandle The resulting key handle.
 * @param[out] Disposition `REG_CREATED_NEW_KEY` or `REG_OPENED_EXISTING_KEY`
 *                         as seen by the caller, may be null.
 * @return Status code.
 */
static NTSTATUS CreateIsolatedKeyEntry(const std::wstring& view_path, const std::wstring& relative,
                                       ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                       PVOID SecurityQualityOfService, ULONG TitleIndex, PUNICODE_STRING Class,
                                       ULONG CreateOptions, HANDLE TransactionHandle, PHANDLE KeyHandle,
                                       PULONG Disposition)
{
    /*
     * `Merge` is the only mode whose create lands in the real registry: a key a
     * sandboxed process creates below a merged key is created in the host when
     * the sandbox does not hold it, which is the write rule of the mode. A
     * transacted call keeps the route of the hive, because the host layer must
     * never be enlisted into the transaction of the caller.
     */
    if (TransactionHandle == nullptr &&
        appbox::registry::Hive::KeyIsolation(relative) == appbox::RegistryKeyIsolation::Merge &&
        !appbox::registry::Hive::SandboxHoldsKey(relative))
    {
        const std::size_t separator = view_path.rfind(L'\\');
        if (separator == std::wstring::npos || separator == 0)
        {
            return STATUS_OBJECT_PATH_INVALID;
        }

        /*
         * The key above the new key is created first: `NtCreateKey` creates a key
         * only when the key above it exists in the registry it names, and the
         * path of the view may be missing in the real registry.
         */
        HANDLE         parent = nullptr;
        const NTSTATUS opened =
            CreateRealKey(view_path.substr(0, separator), KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE, parent);
        if (!NT_SUCCESS(opened))
        {
            return opened;
        }

        appbox::registry::KeyGuard guard(parent);

        const std::wstring name = view_path.substr(separator + 1);

        UNICODE_STRING    us;
        OBJECT_ATTRIBUTES oa;
        sys_RtlInitUnicodeString(&us, name.c_str());
        InitializeObjectAttributes(&oa, &us, Attributes, parent, SecurityDescriptor);
        oa.SecurityQualityOfService = SecurityQualityOfService;

        LOG_D("the key {} is created in the real registry (merge)", appbox::WideToUTF8(relative));
        return sys_NtCreateKey(KeyHandle, DesiredAccess, &oa, TitleIndex, Class, CreateOptions, Disposition);
    }

    const NTSTATUS status =
        CreateHiveKey(relative, DesiredAccess, Attributes, SecurityDescriptor, SecurityQualityOfService, TitleIndex,
                      Class, CreateOptions, TransactionHandle, KeyHandle, Disposition);
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
     *
     * The probe of the host layer is a plain read only open: the host layer is
     * never enlisted into the transaction of the caller.
     */
    const appbox::RegistryKeyIsolation mode = appbox::registry::Hive::KeyIsolation(relative);
    const bool image_holds_key = *Disposition == REG_CREATED_NEW_KEY &&
                                 !appbox::registry::Hive::IsKeyWhitedOut(relative) && LayerHoldsKey(true, relative);
    const bool host_holds_key = *Disposition == REG_CREATED_NEW_KEY && !appbox::registry::KeyHidesHost(mode) &&
                                !appbox::registry::Hive::IsKeyWhitedOut(relative) &&
                                appbox::registry::Hive::HostHoldsKey(view_path);

    *Disposition = appbox::registry::ViewCreateDisposition(mode, *Disposition, image_holds_key, host_holds_key);
    return status;
}

NTSTATUS appbox::registry::Hive::CreateIsolatedKey(const std::wstring& view_path, const std::wstring& relative,
                                                   ACCESS_MASK DesiredAccess, ULONG Attributes,
                                                   PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                                   ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions,
                                                   PHANDLE KeyHandle, PULONG Disposition)
{
    return CreateIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                  SecurityQualityOfService, TitleIndex, Class, CreateOptions, nullptr, KeyHandle,
                                  Disposition);
}

NTSTATUS appbox::registry::Hive::CreateIsolatedKeyTransacted(
    const std::wstring& view_path, const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
    PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex, PUNICODE_STRING Class,
    ULONG CreateOptions, HANDLE TransactionHandle, PHANDLE KeyHandle, PULONG Disposition)
{
    return CreateIsolatedKeyEntry(view_path, relative, DesiredAccess, Attributes, SecurityDescriptor,
                                  SecurityQualityOfService, TitleIndex, Class, CreateOptions, TransactionHandle,
                                  KeyHandle, Disposition);
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

    if (s_hive_data->image_root != nullptr &&
        appbox::registry::StripKeyPrefix(path, s_hive_data->image_mount_name, relative))
    {
        std::wstring translated;
        if (!appbox::registry::MapHivePathToView(relative, s_hive_data->hkcu_prefix, translated))
        {
            return HandleView::NotIsolated;
        }

        view_path = translated;
        return HandleView::ImageHandle;
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
    if (appbox::registry::StripKeyPrefix(object_name, s_hive_data->hive_mount_name, relative))
    {
        return appbox::registry::MapHivePathToView(relative, s_hive_data->hkcu_prefix, view_name);
    }

    if (s_hive_data->image_root != nullptr &&
        appbox::registry::StripKeyPrefix(object_name, s_hive_data->image_mount_name, relative))
    {
        return appbox::registry::MapHivePathToView(relative, s_hive_data->hkcu_prefix, view_name);
    }

    return false;
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

void appbox::registry::Hive::FilterHiddenImageEntries(const std::wstring& view_path, bool values,
                                                      std::vector<std::wstring>& names)
{
    if (s_hive_data == nullptr)
    {
        return;
    }

    std::wstring relative;
    if (!HiveRelativePath(view_path, relative))
    {
        /* Without a hive relative path the whiteouts cannot be looked up, so the
         * layer is presented unchanged. */
        return;
    }

    FilterHiddenLowerEntries(relative, values, names);
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
            st =
                sys_NtEnumerateValueKey(KeyHandle, i, KeyValueBasicInformation, buf.data(), (ULONG)buf.size(), &result);
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

appbox::registry::MergedResolve appbox::registry::Hive::ResolveMergedIndex(HANDLE              KeyHandle,
                                                                           const std::wstring& view_path, bool values,
                                                                           ULONG Index, HANDLE& layer_handle,
                                                                           bool& owned, ULONG& layer_index)
{
    layer_handle = nullptr;
    owned = false;

    const ACCESS_MASK mask = values ? KEY_QUERY_VALUE : KEY_ENUMERATE_SUB_KEYS;

    std::wstring relative;
    const bool   mapped = HiveRelativePath(view_path, relative);

    /*
     * The state hive layer is optional: the caller passes its handle when it
     * holds one, and a handle of the image or of the host leaves the handle
     * null. The layer is opened here in that case, so the merged view reports
     * its entries as well. A key which no layer of the state hive holds has no
     * hive handle, and its merged view is the layers below it alone.
     */
    HANDLE upper = KeyHandle;
    HANDLE owned_upper = nullptr;
    if (upper == nullptr && mapped && TryOpenHiveKey(relative, mask, owned_upper))
    {
        upper = owned_upper;
    }

    std::vector<std::wstring> upper_names;
    if (upper != nullptr)
    {
        const NTSTATUS st = values ? CollectValueNames(upper, upper_names) : CollectSubKeyNames(upper, upper_names);
        if (!NT_SUCCESS(st))
        {
            if (owned_upper != nullptr)
            {
                sys_NtClose(owned_upper);
            }
            return MergedResolve::Error;
        }
    }

    /*
     * The image layer: its entries are visible unless the state of the sandbox
     * deleted them. The kernel index of an entry is kept next to its name,
     * because the merge drops the hidden entries from the visible list while the
     * enumeration of the layer addresses the entry by the index the kernel
     * assigned to it. A layer which cannot be read contributes nothing, which
     * keeps the view consistent with the layers which could be read.
     */
    std::vector<std::wstring> image_names;
    std::vector<ULONG>        image_indices;
    HANDLE                    image = nullptr;
    if (mapped && TryOpenImageKey(relative, mask, image))
    {
        std::vector<std::wstring> image_layer_names;
        const NTSTATUS            st =
            values ? CollectValueNames(image, image_layer_names) : CollectSubKeyNames(image, image_layer_names);
        if (NT_SUCCESS(st))
        {
            image_names.reserve(image_layer_names.size());
            image_indices.reserve(image_layer_names.size());
            for (ULONG index = 0; index < image_layer_names.size(); ++index)
            {
                if (IsHiddenLowerEntry(relative, values, image_layer_names[index]))
                {
                    continue;
                }

                image_names.push_back(image_layer_names[index]);
                image_indices.push_back(index);
            }
        }
        else
        {
            sys_NtClose(image);
            image = nullptr;
        }
    }

    /*
     * The real layer is optional: when the real key does not exist or cannot
     * be read, the merged view degenerates to the sandbox layers alone, which
     * is still a consistent view.
     *
     * The names of the merged view are the entries which stay visible, while
     * the index of an entry inside the real key is the position it has in the
     * unfiltered enumeration of that key: the kernel is asked for the entry at
     * that position.
     *
     * The collectors report every enumerated entry — the default value of a key
     * included, whose name is empty — so the position of an entry inside a
     * collected list is the index the kernel assigned to it.
     */
    std::vector<std::wstring> visible_names;
    std::vector<ULONG>        visible_indices;
    HANDLE                    real = nullptr;
    if (NT_SUCCESS(OpenRealKey(view_path, mask, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
    {
        std::vector<std::wstring> real_names;
        const NTSTATUS st = values ? CollectValueNames(real, real_names) : CollectSubKeyNames(real, real_names);
        if (NT_SUCCESS(st))
        {
            visible_names.reserve(real_names.size());
            visible_indices.reserve(real_names.size());
            for (ULONG index = 0; index < real_names.size(); ++index)
            {
                if (mapped && IsHiddenRealEntry(relative, values, real_names[index]))
                {
                    continue;
                }

                visible_names.push_back(real_names[index]);
                visible_indices.push_back(index);
            }
        }
        else
        {
            sys_NtClose(real);
            real = nullptr;
        }
    }

    EnumLayer layer = EnumLayer::Upper;
    size_t    idx = 0;
    if (!appbox::registry::MapMergedIndex(upper_names, image_names, visible_names, Index, layer, idx))
    {
        if (owned_upper != nullptr)
        {
            sys_NtClose(owned_upper);
        }
        if (image != nullptr)
        {
            sys_NtClose(image);
        }
        if (real != nullptr)
        {
            sys_NtClose(real);
        }
        return MergedResolve::NoMoreEntries;
    }

    switch (layer)
    {
    case EnumLayer::Upper:
        /* The entry lives in the state hive: the handle of the caller answers
         * it when it points at that layer, otherwise the handle opened here. */
        layer_handle = upper;
        owned = owned_upper != nullptr;
        layer_index = (ULONG)idx;
        if (image != nullptr)
        {
            sys_NtClose(image);
        }
        if (real != nullptr)
        {
            sys_NtClose(real);
        }
        return MergedResolve::HiveLayer;

    case EnumLayer::Image:
        layer_handle = image;
        owned = true;
        /* The index inside the image key, not the index inside the visible
         * subset of its entries. */
        layer_index = image_indices[idx];
        if (owned_upper != nullptr)
        {
            sys_NtClose(owned_upper);
        }
        if (real != nullptr)
        {
            sys_NtClose(real);
        }
        return MergedResolve::ImageLayer;

    case EnumLayer::Real:
        /* The index inside the real key, not the index inside the visible
         * subset of its entries. */
        layer_handle = real;
        owned = true;
        layer_index = visible_indices[idx];
        if (owned_upper != nullptr)
        {
            sys_NtClose(owned_upper);
        }
        if (image != nullptr)
        {
            sys_NtClose(image);
        }
        return MergedResolve::RealLayer;
    }

    return MergedResolve::Error;
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

    std::wstring relative;
    const bool   mapped = appbox::registry::Hive::HiveRelativePath(view_path, relative);

    std::vector<std::wstring> image_names;
    HANDLE                    image = nullptr;
    if (mapped && TryOpenImageKey(relative, KEY_ENUMERATE_SUB_KEYS, image))
    {
        appbox::registry::KeyGuard guard(image);
        if (NT_SUCCESS(appbox::registry::Hive::CollectSubKeyNames(image, image_names)))
        {
            FilterHiddenLowerEntries(relative, false, image_names);
        }
        else
        {
            image_names.clear();
        }
    }

    std::vector<std::wstring> real_names;
    HANDLE                    real = nullptr;
    if (NT_SUCCESS(appbox::registry::Hive::OpenRealKey(view_path, KEY_ENUMERATE_SUB_KEYS, OBJ_CASE_INSENSITIVE, nullptr,
                                                       nullptr, &real)))
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

    return appbox::registry::CountMerged(hive_names, image_names, real_names) > 0;
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

NTSTATUS appbox::registry::Hive::SetIsolatedValue(appbox::registry::HandleView handle_view, HANDLE KeyHandle,
                                                  const std::wstring& view_path, const std::wstring& relative,
                                                  const std::wstring& value_name, ULONG TitleIndex, ULONG Type,
                                                  PVOID Data, ULONG DataSize)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());

    /*
     * The layer of the write is the one the isolation mode of the value decides
     * (`ValueWritesToHost`): a value no sandbox layer holds below a merged key
     * is written into the real registry, every other value into the state hive
     * of the sandbox.
     */
    const bool host_handle = handle_view == appbox::registry::HandleView::RealHandle;
    const bool to_host = ValueWritesToHost(relative, value_name);

    if (to_host)
    {
        if (host_handle)
        {
            return sys_NtSetValueKey(KeyHandle, &us, TitleIndex, Type, Data, DataSize);
        }

        /*
         * The granted access of the handle of the caller is tested first: a
         * caller which opened the key without the right to write must not gain
         * it through the isolation.
         */
        if (!HandlePermitsValueWrite(KeyHandle))
        {
            return STATUS_ACCESS_DENIED;
        }

        HANDLE   key = nullptr;
        NTSTATUS status = CreateRealKey(view_path, KEY_SET_VALUE | KEY_QUERY_VALUE, key);
        LOG_D("the value {} of {} is written in the real registry (merge): {:#x}", appbox::WideToUTF8(value_name),
              appbox::WideToUTF8(relative), status);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        appbox::registry::KeyGuard guard(key);
        return sys_NtSetValueKey(key, &us, TitleIndex, Type, Data, DataSize);
    }

    /* The write lands in the sandbox, and the state hive is the layer of it. */
    if (handle_view == appbox::registry::HandleView::HiveHandle)
    {
        return sys_NtSetValueKey(KeyHandle, &us, TitleIndex, Type, Data, DataSize);
    }

    /*
     * A handle of the image cannot carry the write: the image is read-only, so
     * the key is copied up into the state hive first. The values the image
     * holds stay visible through the merged view of the copied up key.
     */
    if (handle_view == appbox::registry::HandleView::ImageHandle)
    {
        if (!HandlePermitsValueWrite(KeyHandle))
        {
            return STATUS_ACCESS_DENIED;
        }

        HANDLE   key = nullptr;
        NTSTATUS status = CreateHiveKey(relative, KEY_SET_VALUE | KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr,
                                        nullptr, 0, nullptr, REG_OPTION_NON_VOLATILE, nullptr, &key, nullptr);
        if (!NT_SUCCESS(status))
        {
            LOG_E("failed to copy the key {} of the image up for a value write: {:#x}", appbox::WideToUTF8(relative),
                  status);
            return status;
        }

        appbox::registry::KeyGuard guard(key);
        return sys_NtSetValueKey(key, &us, TitleIndex, Type, Data, DataSize);
    }

    return STATUS_ACCESS_DENIED;
}

NTSTATUS appbox::registry::Hive::DeleteIsolatedKey(appbox::registry::HandleView handle_view, HANDLE KeyHandle,
                                                   const std::wstring& view_path, const std::wstring& relative)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    const bool host_handle = handle_view == appbox::registry::HandleView::RealHandle;
    const bool upper_handle = handle_view == appbox::registry::HandleView::HiveHandle;

    /*
     * `Merge` is the only mode whose delete reaches the real registry, and it
     * does so when no sandbox layer holds the key: the handle of the caller is
     * a handle of the host layer in that case, because the open of such a key is
     * answered by the host layer, so the kernel runs the real delete with the
     * rights of that handle.
     */
    if (KeyIsolation(relative) == RegistryKeyIsolation::Merge && !SandboxHoldsKey(relative))
    {
        if (!host_handle)
        {
            /* The key lives in the host, so a handle of a sandbox layer cannot
             * name it. */
            return STATUS_ACCESS_DENIED;
        }

        LOG_D("the key {} is deleted in the real registry (merge)", appbox::WideToUTF8(relative));
        return sys_NtDeleteKey(KeyHandle);
    }

    if (host_handle)
    {
        /* Every other route runs against the sandbox, and a handle of the host
         * layer does not point at the sandbox. */
        return STATUS_ACCESS_DENIED;
    }

    /*
     * A key which still holds sub keys cannot be deleted, and the caller sees
     * the sub keys of every layer. Values do not block a delete. The check runs
     * before the kernel is asked, because the kernel only knows the sub keys of
     * the layer of its handle.
     */
    if (MergedViewHoldsSubKeys(upper_handle ? KeyHandle : nullptr, view_path))
    {
        return STATUS_CANNOT_DELETE;
    }

    /*
     * The state hive layer is removed first: through the handle of the caller
     * when it points at that layer, which also makes the kernel check its
     * rights, and through a handle of the layer otherwise, because the handle
     * of the image names the read-only layer.
     */
    if (upper_handle)
    {
        const NTSTATUS status = sys_NtDeleteKey(KeyHandle);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }
    else
    {
        /*
         * A write access open of a key of the image copies the key up, so a
         * handle of the image never carries the right to delete in the normal
         * path. A handle which was duplicated into the sandbox is tested here.
         */
        if (!HandlePermitsKeyDelete(KeyHandle))
        {
            return STATUS_ACCESS_DENIED;
        }

        HANDLE key = nullptr;
        if (TryOpenHiveKey(relative, DELETE, key))
        {
            appbox::registry::KeyGuard guard(key);
            const NTSTATUS             status = sys_NtDeleteKey(key);
            if (!NT_SUCCESS(status))
            {
                return status;
            }
        }
    }

    /*
     * The state hive no longer holds the key, while the image and the host may.
     * The host layer is only probed when the mode keeps it visible.
     */
    const bool lower_holds = !IsKeyWhitedOut(relative) && LayerHoldsKey(true, relative);
    const bool host_holds = !appbox::registry::KeyHidesHost(KeyIsolation(relative)) && HostHoldsKey(view_path);
    const appbox::registry::DeleteTarget target =
        appbox::registry::KeyDeleteOutcomeOf(KeyIsolation(relative), true, lower_holds, host_holds);

    if (target == appbox::registry::DeleteTarget::SandboxAndWhiteout && !RecordKeyWhiteout(relative))
    {
        /* Failing closed: reporting a success would let the entry of the layer
         * below reappear in the view of the sandbox. */
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS appbox::registry::Hive::DeleteIsolatedValue(appbox::registry::HandleView handle_view, HANDLE KeyHandle,
                                                     const std::wstring& view_path, const std::wstring& relative,
                                                     const std::wstring& value_name)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, value_name.c_str());

    /*
     * The delete of a value the sandbox does not hold below a merged key lands in
     * the real registry, which is the write rule of the mode. A handle of the
     * host layer is forwarded — the kernel checks its rights — while a handle of
     * the hive is re-routed to the key of the host layer, which is opened
     * without creating it: a delete never creates the key it acts on.
     */
    if (ValueWritesToHost(relative, value_name))
    {
        if (handle_view == appbox::registry::HandleView::RealHandle)
        {
            return sys_NtDeleteValueKey(KeyHandle, &us);
        }

        if (!HandlePermitsValueWrite(KeyHandle))
        {
            return STATUS_ACCESS_DENIED;
        }

        HANDLE real = nullptr;
        if (!NT_SUCCESS(appbox::registry::Hive::OpenRealKey(view_path, KEY_SET_VALUE | KEY_QUERY_VALUE,
                                                            OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
        {
            /* The host does not hold the key of the value, so the value does not
             * exist for the caller either. */
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        appbox::registry::KeyGuard guard(real);
        LOG_D("the value {} of {} is deleted in the real registry (merge)", appbox::WideToUTF8(value_name),
              appbox::WideToUTF8(relative));
        return sys_NtDeleteValueKey(real, &us);
    }

    if (handle_view == appbox::registry::HandleView::RealHandle)
    {
        /* A handle of the host layer does not point at the sandbox, and the
         * route above is the only one which may act on the host layer. */
        return STATUS_ACCESS_DENIED;
    }

    /*
     * The state of the view before the delete: a value the sandbox deleted
     * counts as held, because the deletion is recorded in the whiteout store.
     */
    const bool sandbox_holds = SandboxHoldsValue(relative, value_name);
    const bool host_holds = !HidesHostValue(relative, value_name) && HostHoldsValue(view_path, value_name);

    /*
     * The value of the state hive is removed first: through the handle of the
     * caller when it points at that layer, which also makes the kernel check its
     * rights, and through a handle of the layer otherwise, because the handle of
     * the image names the read-only layer. A value the state hive does not hold
     * is not an error here: the image holds it and the marker hides it.
     */
    if (handle_view == appbox::registry::HandleView::HiveHandle)
    {
        const NTSTATUS status = DeleteHiveValue(KeyHandle, value_name);
        if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            return status;
        }
    }
    else
    {
        if (!HandlePermitsValueWrite(KeyHandle))
        {
            return STATUS_ACCESS_DENIED;
        }

        HANDLE key = nullptr;
        if (TryOpenHiveKey(relative, KEY_SET_VALUE | KEY_QUERY_VALUE, key))
        {
            appbox::registry::KeyGuard guard(key);
            DeleteHiveValue(key, value_name);
        }
    }

    const bool lower_holds = !IsValueWhitedOut(relative, value_name) && LayerHoldsValue(true, relative, value_name);
    const appbox::registry::DeleteTarget target = appbox::registry::ValueDeleteOutcomeOf(
        ListedValueIsolation(relative, value_name), KeyIsolation(relative), sandbox_holds, lower_holds, host_holds);

    switch (target)
    {
    case appbox::registry::DeleteTarget::ReportMissing:
        /* The value does not exist in the view of the sandbox. */
        return STATUS_OBJECT_NAME_NOT_FOUND;

    case appbox::registry::DeleteTarget::SandboxOnly:
        /* The value of the state hive is gone and no lower layer holds it. */
        return STATUS_SUCCESS;

    case appbox::registry::DeleteTarget::WhiteoutOnly:
    case appbox::registry::DeleteTarget::SandboxAndWhiteout:
        if (!RecordValueWhiteout(relative, value_name))
        {
            /* Failing closed: the value of the layer below would reappear. */
            return STATUS_UNSUCCESSFUL;
        }
        return STATUS_SUCCESS;

    case appbox::registry::DeleteTarget::HostDelete:
        /* The route of the host layer was answered above. */
        LOG_E("the delete of the value {} of {} was routed to the wrong layer", appbox::WideToUTF8(value_name),
              appbox::WideToUTF8(relative));
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Copy a key of the read-only image into the state hive.
 *
 * A rename moves the key of the state hive, so the content of a key the image
 * holds has to travel into that layer first: the marker of the old name hides
 * the image afterwards, which would leave the renamed key empty.
 *
 * The values the state hive already holds are kept, because the state of the
 * sandbox is the highest layer of the view. The sub keys are created in the
 * state hive and their content is copied the same way, so the whole subtree
 * travels.
 *
 * @param[in] relative The key path relative to the hive root.
 * @param[in] depth The recursion depth of the copy.
 * @return Status code; `STATUS_SUCCESS` when the image does not hold the key.
 */
static NTSTATUS CopyUpImageKey(const std::wstring& relative, ULONG depth)
{
    /** Deepest key path the copy follows, a guard against a hostile hive. */
    constexpr ULONG kMaxDepth = 256;
    if (depth > kMaxDepth)
    {
        return STATUS_OBJECT_PATH_INVALID;
    }

    HANDLE image = nullptr;
    if (!TryOpenImageKey(relative, KEY_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, image))
    {
        return STATUS_SUCCESS;
    }
    appbox::registry::KeyGuard image_guard(image);

    HANDLE   upper = nullptr;
    NTSTATUS status = CreateHiveKey(relative, KEY_ALL_ACCESS, OBJ_CASE_INSENSITIVE, nullptr, nullptr, 0, nullptr,
                                    REG_OPTION_NON_VOLATILE, nullptr, &upper, nullptr);
    if (!NT_SUCCESS(status))
    {
        LOG_E("failed to copy the key {} of the image up: {:#x}", appbox::WideToUTF8(relative), status);
        return status;
    }
    appbox::registry::KeyGuard upper_guard(upper);

    std::vector<std::wstring> values;
    status = appbox::registry::Hive::CollectValueNames(image, values);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    for (const auto& name : values)
    {
        if (HiveHoldsValue(upper, name))
        {
            /* The state of the sandbox wins over the content of the image. */
            continue;
        }

        ULONG             type = REG_NONE;
        std::vector<BYTE> data;
        status = ReadValueFull(image, name, type, data);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        status = WriteValue(upper, name, type, data);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    std::vector<std::wstring> keys;
    status = appbox::registry::Hive::CollectSubKeyNames(image, keys);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    for (const auto& name : keys)
    {
        status = CopyUpImageKey(appbox::registry::JoinKeyPath(relative, name), depth + 1);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS appbox::registry::Hive::RenameIsolatedKey(appbox::registry::HandleView handle_view, HANDLE KeyHandle,
                                                   const std::wstring& view_path, const std::wstring& relative,
                                                   const std::wstring& new_name)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    const bool host_handle = handle_view == appbox::registry::HandleView::RealHandle;

    /*
     * The name of a root key of the view is the first component of every path
     * of the hive, so a rename of one of them would make the whole subtree
     * unreachable for the view: the mapping of a handle and of an open both
     * start with that name. The kernel refuses the rename of the root of a
     * hive the same way.
     */
    const std::wstring parent_relative = appbox::registry::ParentKeyPath(relative);
    if (parent_relative.empty())
    {
        return STATUS_ACCESS_DENIED;
    }

    /*
     * The new name of a key is a single component: a rename never moves a key
     * into another key, so the kernel refuses a name which carries a separator
     * with STATUS_INVALID_PARAMETER.
     */
    if (new_name.empty() || new_name.find(L'\\') != std::wstring::npos)
    {
        return STATUS_INVALID_PARAMETER;
    }

    const std::wstring new_relative = appbox::registry::JoinKeyPath(parent_relative, new_name);
    const std::wstring new_view_path =
        appbox::registry::JoinKeyPath(appbox::registry::ParentKeyPath(view_path), new_name);

    /*
     * The destination of the rename is a key of the merged view: a key which
     * the hive holds blocks the rename inside the kernel, and a key which only
     * the host holds blocks it here when the mode keeps the host entry
     * visible. The kernel reports an existing destination key as
     * STATUS_CANNOT_DELETE, which is the answer the caller observes for a
     * destination of either layer — a renamed key must never be merged with a
     * visible key of the host layer.
     */
    const bool destination_in_hive = SandboxHoldsKey(new_relative) && !IsKeyWhitedOut(new_relative);
    const bool destination_in_host = !appbox::registry::KeyHidesHost(KeyIsolation(new_relative)) &&
                                     !IsKeyWhitedOut(new_relative) && HostHoldsKey(new_view_path);

    /*
     * The name is passed through a local structure, so the call never reads the
     * memory of the caller again.
     */
    UNICODE_STRING new_name_unicode;
    sys_RtlInitUnicodeString(&new_name_unicode, new_name.c_str());

    /*
     * `Merge` renames the key of the real registry when the sandbox does not
     * hold it, which is the write rule of the mode: the handle of the caller is a
     * handle of the host layer in that case, because the open of such a key is
     * answered by the host layer.
     */
    if (KeyIsolation(relative) == RegistryKeyIsolation::Merge && !SandboxHoldsKey(relative))
    {
        if (!host_handle)
        {
            /* The key lives in the host, so a handle of the hive cannot name it. */
            return STATUS_ACCESS_DENIED;
        }

        if (destination_in_hive || destination_in_host)
        {
            return STATUS_CANNOT_DELETE;
        }

        LOG_D("the key {} is renamed in the real registry (merge)", appbox::WideToUTF8(relative));
        return sys_NtRenameKey(KeyHandle, &new_name_unicode);
    }

    if (host_handle)
    {
        /* Every other route runs inside the hive, and a handle of the host layer
         * does not point at the hive. */
        return STATUS_ACCESS_DENIED;
    }

    if (destination_in_host)
    {
        return STATUS_CANNOT_DELETE;
    }

    /*
     * The rename moves the key of the state hive, so the content of a key the
     * image holds is copied up first: the image is never written, and the
     * marker of the old name hides it afterwards, which would leave the renamed
     * key empty without the copy. The copy runs for a handle of either sandbox
     * layer, because the open of a write access key only creates the shadow key
     * and not the content of the image.
     */
    if (handle_view == appbox::registry::HandleView::ImageHandle && !HandlePermitsKeyDelete(KeyHandle))
    {
        return STATUS_ACCESS_DENIED;
    }

    NTSTATUS status = CopyUpImageKey(relative, 0);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    HANDLE rename_key = KeyHandle;
    HANDLE owned = nullptr;
    if (handle_view == appbox::registry::HandleView::ImageHandle)
    {
        status = CreateHiveKey(relative, DELETE | KEY_READ | KEY_WRITE, OBJ_CASE_INSENSITIVE, nullptr, nullptr, 0,
                               nullptr, REG_OPTION_NON_VOLATILE, nullptr, &owned, nullptr);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
        rename_key = owned;
    }

    /*
     * The kernel removes the key from its old name and creates it under the
     * new name, and it checks the rights of the handle of the caller, so a
     * handle which does not permit a rename reports the failure of the real
     * call. A destination the state hive holds is refused by the kernel as well.
     */
    status = sys_NtRenameKey(rename_key, &new_name_unicode);
    CloseHiveHandle(owned);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    /*
     * The old name is the delete of the key in the merged view: the key of the
     * state hive is gone from it, so an entry of a layer below has to be
     * recorded as deleted as well — without the marker the merged view would
     * report the entry of the old name next to the renamed key. The route is the
     * one of the delete of a key, which is why the sandbox side is passed as
     * held: the handle of the caller proves it.
     */
    const bool lower_holds = !IsKeyWhitedOut(relative) && LayerHoldsKey(true, relative);
    const bool host_holds = !appbox::registry::KeyHidesHost(KeyIsolation(relative)) && HostHoldsKey(view_path);
    if (appbox::registry::KeyDeleteOutcomeOf(KeyIsolation(relative), true, lower_holds, host_holds) ==
            appbox::registry::DeleteTarget::SandboxAndWhiteout &&
        !RecordKeyWhiteout(relative))
    {
        /* Failing closed: reporting a success would let the entry of the layer
         * below reappear under the old name. */
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS appbox::registry::Hive::QueryMultipleValues(appbox::registry::HandleView handle_view, HANDLE KeyHandle,
                                                     const std::wstring& view_path, const std::wstring& relative,
                                                     PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount, PVOID ValueBuffer,
                                                     PULONG BufferLength, PULONG RequiredBufferLength)
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
     * The handles of the layers the batch may be read from. The handle of the
     * caller answers the batch when every value lives in its layer, otherwise
     * the layers are opened here and the batch is assembled below.
     */
    HANDLE owned_upper = nullptr;
    HANDLE upper = handle_view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr;
    if (upper == nullptr && TryOpenHiveKey(relative, KEY_QUERY_VALUE, owned_upper))
    {
        upper = owned_upper;
    }
    appbox::registry::KeyGuard upper_guard(owned_upper);

    HANDLE image = nullptr;
    TryOpenImageKey(relative, KEY_QUERY_VALUE, image);
    appbox::registry::KeyGuard image_guard(image);

    /** The layer of one entry of the batch. */
    enum EntryLayer
    {
        kUpper = 0,
        kImage = 1,
        kHost = 2,
    };

    /*
     * Classify every entry: the state hive wins over the image, which wins over
     * the host layer, and a value which the isolation hides is missing in the
     * view.
     */
    std::vector<std::wstring> names(EntryCount);
    std::vector<int>          owners(EntryCount, kHost);
    bool                      any_upper = false;
    bool                      any_image = false;
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

        if (HiveHoldsValue(upper, names[index]))
        {
            owners[index] = kUpper;
            any_upper = true;
            continue;
        }

        if (HiveHoldsValue(image, names[index]))
        {
            owners[index] = kImage;
            any_image = true;
            continue;
        }

        if (IsHiddenRealEntry(relative, true, names[index]) || !HostHoldsValue(view_path, names[index]))
        {
            /* A value which is missing in the view fails the whole batch, which
             * is what the kernel reports for a batch which names a value the
             * key does not hold. */
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        owners[index] = kHost;
        any_host = true;
    }

    if (!any_host && !any_image && handle_view == appbox::registry::HandleView::HiveHandle)
    {
        /* The whole batch lives in the layer of the caller, the kernel answers it. */
        return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    if (!any_host && !any_upper && handle_view == appbox::registry::HandleView::ImageHandle)
    {
        /* The whole batch lives in the image, which is the layer of the caller. */
        return sys_NtQueryMultipleValueKey(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    HANDLE real = nullptr;
    if (any_host)
    {
        if (!NT_SUCCESS(OpenRealKey(view_path, KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
        {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }

    appbox::registry::KeyGuard real_guard(real);

    if (!any_upper && !any_image)
    {
        /* The whole batch lives in the host layer, the kernel answers it. */
        return sys_NtQueryMultipleValueKey(real, ValueEntries, EntryCount, ValueBuffer, BufferLength,
                                           RequiredBufferLength);
    }

    /*
     * The batch mixes the layers, so it is assembled here: every value is read
     * from the layer which holds it and the data is packed into the caller
     * buffer the way the kernel packs it (the entries follow each other without
     * padding).
     */
    std::vector<std::vector<BYTE>> datas(EntryCount);
    std::vector<ULONG>             types(EntryCount, REG_NONE);
    unsigned long long             total = 0;

    for (ULONG index = 0; index < EntryCount; ++index)
    {
        const HANDLE   owner = owners[index] == kUpper ? upper : (owners[index] == kImage ? image : real);
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

    auto* out = static_cast<BYTE*>(ValueBuffer);
    ULONG offset = 0;
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

NTSTATUS appbox::registry::Hive::SaveIsolatedKey(appbox::registry::HandleView handle_view, HANDLE KeyHandle,
                                                 const std::wstring& view_path, const std::wstring& relative,
                                                 HANDLE FileHandle, ULONG Format, bool extended)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }

    /*
     * The state hive layer of the key: the handle of the caller when it points
     * at that layer, otherwise a handle opened here. A key which only the image
     * or the host holds has no state hive layer at all.
     */
    HANDLE owned = nullptr;
    HANDLE hive_key = handle_view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr;
    if (hive_key == nullptr && TryOpenHiveKey(relative, KEY_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, owned))
    {
        hive_key = owned;
    }

    /*
     * The fast path: when neither the image nor the host holds the key, the
     * state hive layer is the whole view and the key can be saved directly.
     */
    if (hive_key != nullptr && !LayerHoldsKey(true, relative) && !HostHoldsKey(view_path))
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
    NTSTATUS status =
        CreateHiveKeyRelative(scratch_handle, LastKeyComponent(relative), KEY_ALL_ACCESS, OBJ_CASE_INSENSITIVE, nullptr,
                              nullptr, 0, nullptr, REG_OPTION_NON_VOLATILE, nullptr, &snapshot, nullptr);
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
