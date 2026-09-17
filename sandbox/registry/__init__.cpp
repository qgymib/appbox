#include "utils/WinAPI.h" /* Must be first include file */
#include <vector>
#include "hook/NtCreateKey.hpp"
#include "hook/NtEnumerateKey.hpp"
#include "hook/NtEnumerateValueKey.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtOpenKey.hpp"
#include "hook/NtOpenKeyEx.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "registry/EnumMerge.hpp"
#include "registry/KeyPath.hpp"
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
 * @brief Duplicates a handle into the current process.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwduplicateobject
 */
typedef NTSTATUS (*T_NtDuplicateObject)(
    /* [IN] */  HANDLE      SourceProcessHandle,
    /* [IN] */  HANDLE      SourceHandle,
    /* [IN,OPT] */ HANDLE   TargetProcessHandle,
    /* [OUT] */ PHANDLE     TargetHandle,
    /* [IN] */  ACCESS_MASK DesiredAccess,
    /* [IN] */  ULONG       HandleAttributes,
    /* [IN] */  ULONG       Options);

/**
 * @brief The kernel object name of the root of every application hive.
 */
static const wchar_t* s_app_hive_root_name = L"\\REGISTRY\\A";

/**
 * @brief Runtime state of the registry hive module.
 */
struct appbox::registry::Hive::Data
{
    HANDLE       hive_root = nullptr;  /* Root key handle of the private hive mount. */
    std::wstring hive_mount_name;      /* Object name of the mount, for example \REGISTRY\A\{GUID}. */
    std::wstring hkcu_prefix;           /* NT path of the real HKCU root, \REGISTRY\USER\<SID>. */
    std::wstring hive_path;             /* DOS path of the hive file. */
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

    s_hive_data = data;

    LOG_I("registry hive mounted: {} (mount: {}, hkcu: {})", appbox::WideToUTF8(data->hive_path),
          appbox::WideToUTF8(data->hive_mount_name), appbox::WideToUTF8(data->hkcu_prefix));
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
        return HiveMap::NotHkcu;
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
            return HiveMap::NotHkcu;
        }

        /*
         * Handles which were handed out by the hooks refer to keys inside the
         * hive. Translate them back into the view, so the key behaves exactly
         * like the key it shadows.
         */
        std::wstring hive_relative;
        if (appbox::registry::StripKeyPrefix(root, s_hive_data->hive_mount_name, hive_relative))
        {
            path = appbox::registry::JoinKeyPath(s_hive_data->hkcu_prefix, hive_relative);
        }
        else
        {
            path = root;
        }

        path = appbox::registry::JoinKeyPath(path, name);
    }

    if (!appbox::registry::StripKeyPrefix(path, s_hive_data->hkcu_prefix, relative))
    {
        return HiveMap::NotHkcu;
    }

    view_path = path;
    return HiveMap::Isolated;
}

/**
 * @brief Duplicate the hive root handle for callers which open HKCU itself.
 * @param[out] KeyHandle The duplicated handle.
 * @return Status code.
 */
static NTSTATUS DuplicateHiveRoot(PHANDLE KeyHandle)
{
    auto fn = reinterpret_cast<T_NtDuplicateObject>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                   "NtDuplicateObject"));
    if (fn == nullptr)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    return fn((HANDLE)-1, s_hive_data->hive_root, (HANDLE)-1, KeyHandle, 0, 0, DUPLICATE_SAME_ACCESS);
}

NTSTATUS appbox::registry::Hive::OpenKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                         PVOID SecurityDescriptor, PVOID SecurityQualityOfService,
                                         PHANDLE KeyHandle)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (relative.empty())
    {
        /* HKCU itself was opened: hand out a copy of the hive root handle. */
        return DuplicateHiveRoot(KeyHandle);
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
    if (relative.empty())
    {
        /* HKCU itself was opened: hand out a copy of the hive root handle. */
        return DuplicateHiveRoot(KeyHandle);
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

NTSTATUS appbox::registry::Hive::CreateKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                          PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex,
                                          PUNICODE_STRING Class, ULONG CreateOptions, PHANDLE KeyHandle,
                                          PULONG Disposition)
{
    if (s_hive_data == nullptr)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (relative.empty())
    {
        /* HKCU itself always exists. */
        NTSTATUS st = DuplicateHiveRoot(KeyHandle);
        if (NT_SUCCESS(st) && Disposition != nullptr)
        {
            *Disposition = REG_OPENED_EXISTING_KEY;
        }
        return st;
    }

    UNICODE_STRING    us;
    OBJECT_ATTRIBUTES oa;
    sys_RtlInitUnicodeString(&us, relative.c_str());
    InitializeObjectAttributes(&oa, &us, Attributes, s_hive_data->hive_root, SecurityDescriptor);
    oa.SecurityQualityOfService = SecurityQualityOfService;

    /*
     * NtCreateKey opens the key when it exists and creates it, including every
     * intermediate key, when it does not. Disposition is reported accordingly.
     */
    return sys_NtCreateKey(KeyHandle, DesiredAccess, &oa, TitleIndex, Class, CreateOptions, Disposition);
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
        view_path = appbox::registry::JoinKeyPath(s_hive_data->hkcu_prefix, relative);
        return HandleView::HiveHandle;
    }

    if (appbox::registry::StripKeyPrefix(path, s_hive_data->hkcu_prefix, relative))
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

    view_name = appbox::registry::JoinKeyPath(s_hive_data->hkcu_prefix, relative);
    return true;
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
        if (info->NameLength == 0)
        {
            continue;
        }
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
        if (info->NameLength == 0)
        {
            continue;
        }
        names.emplace_back(info->Name, info->NameLength / sizeof(WCHAR));
    }
}

appbox::registry::MergedResolve appbox::registry::Hive::ResolveMergedIndex(HANDLE KeyHandle,
                                                                           const std::wstring& view_path, bool values,
                                                                           ULONG Index, HANDLE& real_handle,
                                                                           ULONG& layer_index)
{
    real_handle = nullptr;

    std::vector<std::wstring> hive_names;
    NTSTATUS st = values ? CollectValueNames(KeyHandle, hive_names) : CollectSubKeyNames(KeyHandle, hive_names);
    if (!NT_SUCCESS(st))
    {
        return MergedResolve::Error;
    }

    /*
     * The real layer is optional: when the real key does not exist or cannot
     * be read, the merged view degenerates to the hive layer alone, which is
     * still a consistent view.
     */
    std::vector<std::wstring> real_names;
    HANDLE                    real = nullptr;
    ACCESS_MASK               mask = values ? KEY_QUERY_VALUE : KEY_ENUMERATE_SUB_KEYS;
    if (NT_SUCCESS(OpenRealKey(view_path, mask, OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
    {
        st = values ? CollectValueNames(real, real_names) : CollectSubKeyNames(real, real_names);
        if (NT_SUCCESS(st))
        {
            real_handle = real;
        }
        else
        {
            sys_NtClose(real);
        }
    }

    EnumLayer layer = EnumLayer::Hive;
    size_t    idx = 0;
    if (!appbox::registry::MapMergedIndex(hive_names, real_names, Index, layer, idx))
    {
        if (real_handle != nullptr)
        {
            sys_NtClose(real_handle);
            real_handle = nullptr;
        }
        return MergedResolve::NoMoreEntries;
    }

    layer_index = (ULONG)idx;
    if (layer == EnumLayer::Real)
    {
        return MergedResolve::RealLayer;
    }

    /* The entry lives in the hive layer, the real handle is not needed. */
    if (real_handle != nullptr)
    {
        sys_NtClose(real_handle);
        real_handle = nullptr;
    }
    return MergedResolve::HiveLayer;
}
