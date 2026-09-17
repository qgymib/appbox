#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "hook/NtCreateFile.hpp"
#include "NtQueryObject.hpp"

T_NtQueryObject sys_NtQueryObject = nullptr;

static nlohmann::json NtQueryObjectLogParam(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                            PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    nlohmann::json json;
    json["Handle"] = appbox::PointerToString(Handle);
    json["ObjectInformationClass"] = ObjectInformationClass;
    json["ObjectInformation"] = appbox::PointerToString(ObjectInformation);
    json["ObjectInformationLength"] = ObjectInformationLength;
    json["ReturnLength"] = appbox::PointerToString(ReturnLength);
    return json;
}

static appbox::LoggerF logger("NtQueryObject", NtQueryObjectLogParam);

/**
 * @brief Size of the local buffer which holds one object name record.
 *
 * Registry key paths are far below this size. File paths which exceed it fall
 * back to a forwarded call, so the translation never truncates a name.
 */
static const ULONG kObjectNameBufferSize = 0x2000;

/**
 * @brief Answer an ObjectNameInformation query with hive names translated.
 *
 * The query runs into a local buffer first, so the result can be translated
 * regardless of the size of the caller buffer. Object names below the private
 * hive mount (\REGISTRY\A\{GUID}\...) are rewritten into the logical view path
 * (\REGISTRY\USER\<SID>\...), which hides the implementation detail of the
 * registry isolation from the sandboxed process. Every other name is copied
 * verbatim, which is byte for byte what a forwarded call would report.
 *
 * @param[in] Handle The object handle of the original call.
 * @param[out] ObjectInformation The caller buffer, may be null.
 * @param[in] ObjectInformationLength The size of the caller buffer.
 * @param[out] ReturnLength The size of the answer.
 * @return Status code.
 */
static NTSTATUS QueryObjectNameIsolated(HANDLE Handle, PVOID ObjectInformation, ULONG ObjectInformationLength,
                                        PULONG ReturnLength)
{
    BYTE  local[kObjectNameBufferSize];
    ULONG needed = 0;
    NTSTATUS st = sys_NtQueryObject(Handle, ObjectNameInformation, local, kObjectNameBufferSize, &needed);
    if (!NT_SUCCESS(st) || needed < sizeof(OBJECT_NAME_INFORMATION))
    {
        /* The name exceeds the local buffer (for example a long file path) or
         * the query failed: forward the original call unchanged. */
        return sys_NtQueryObject(Handle, ObjectNameInformation, ObjectInformation, ObjectInformationLength,
                                 ReturnLength);
    }

    auto* info = reinterpret_cast<OBJECT_NAME_INFORMATION*>(local);
    if (info->Name.Buffer == nullptr || info->Name.Length == 0)
    {
        /* Unnamed objects have nothing to translate. */
        if (ReturnLength != nullptr)
        {
            *ReturnLength = needed;
        }
        if (ObjectInformation != nullptr && ObjectInformationLength >= needed)
        {
            memcpy(ObjectInformation, local, needed);
            return STATUS_SUCCESS;
        }
        return STATUS_BUFFER_OVERFLOW;
    }

    std::wstring object_name;
    object_name.assign(info->Name.Buffer, info->Name.Length / sizeof(wchar_t));

    std::wstring view_name;
    if (!appbox::registry::Hive::TranslateHiveObjectName(object_name, view_name))
    {
        /* The object is not below the hive mount: return the name verbatim. */
        if (ReturnLength != nullptr)
        {
            *ReturnLength = needed;
        }
        if (ObjectInformation != nullptr && ObjectInformationLength >= needed)
        {
            memcpy(ObjectInformation, local, needed);
            return STATUS_SUCCESS;
        }
        return STATUS_BUFFER_OVERFLOW;
    }

    /* Rebuild the record with the translated name behind the string header. */
    const ULONG name_offset   = sizeof(UNICODE_STRING);
    const ULONG new_name_len  = (ULONG)(view_name.size() * sizeof(WCHAR));
    const ULONG new_needed    = name_offset + new_name_len;

    if (ReturnLength != nullptr)
    {
        *ReturnLength = new_needed;
    }
    if (ObjectInformation == nullptr || ObjectInformationLength < new_needed)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    auto* out = reinterpret_cast<OBJECT_NAME_INFORMATION*>(ObjectInformation);
    out->Name.Length         = (USHORT)new_name_len;
    out->Name.MaximumLength  = (USHORT)new_name_len;
    out->Name.Buffer         = reinterpret_cast<PWSTR>(reinterpret_cast<BYTE*>(ObjectInformation) + name_offset);
    memcpy(out->Name.Buffer, view_name.c_str(), new_name_len);
    return STATUS_SUCCESS;
}

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    if (appbox::ThreadLocal::Get().disable_NtQueryObject_hook)
    {
        return sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength,
                                 ReturnLength);
    }

    {
        appbox::NtCreateFileLock lock;
        logger.Log(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);
    }

    if (ObjectInformationClass == ObjectNameInformation && appbox::registry::Hive::IsEnabled())
    {
        return QueryObjectNameIsolated(Handle, ObjectInformation, ObjectInformationLength, ReturnLength);
    }

    return sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength,
                             ReturnLength);
}

static void LoadNtQueryObject()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryObject");
    sys_NtQueryObject = reinterpret_cast<T_NtQueryObject>(addr);
}

appbox::HookRecord appbox::HookNtQueryObject = {
    "NtQueryObject",
    LoadNtQueryObject,
    (void**)&sys_NtQueryObject,
    Hook_NtQueryObject,
};
