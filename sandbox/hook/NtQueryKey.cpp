#include "WinAPI.h" /* Must be first include file */
#include <vector>
#include "registry/__init__.hpp"
#include "registry/EnumMerge.hpp"
#include "registry/KeyGuard.hpp"
#include "utils/Log.hpp"
#include "NtQueryKey.hpp"

T_NtQueryKey sys_NtQueryKey = nullptr;

static nlohmann::json NtQueryKeyLogParam(HANDLE KeyHandle, KEY_INFORMATION_CLASS KeyInformationClass,
                                         PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["KeyInformationClass"] = KeyInformationClass;
    json["KeyInformation"] = appbox::PointerToString(KeyInformation);
    json["Length"] = Length;
    json["ResultLength"] = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtQueryKey", NtQueryKeyLogParam);

/**
 * @brief Size of the local buffer which holds a complete key information
 *        record of a name information class.
 *
 * Key paths of the hive are far below this size, so a single query suffices.
 */
static const ULONG kKeyNameBufferSize = 0x2000;

/**
 * @brief Field layout of a key information structure with a name.
 *
 * @param[in] info_class The information class of the query.
 * @param[out] name_len_offset The offset of the NameLength field.
 * @param[out] name_offset The offset of the Name field.
 * @return true for the name information classes.
 */
static bool NameLayoutOf(KEY_INFORMATION_CLASS info_class, ULONG& name_len_offset, ULONG& name_offset)
{
    switch (info_class)
    {
    case KeyNameInformation:
        /* ULONG NameLength; WCHAR Name[1]; */
        name_len_offset = 0;
        name_offset = 4;
        return true;
    case KeyBasicInformation:
        /* LARGE_INTEGER LastWriteTime; ULONG TitleIndex; ULONG NameLength; WCHAR Name[1]; */
        name_len_offset = 12;
        name_offset = 16;
        return true;
    case KeyNodeInformation:
        /* LARGE_INTEGER LastWriteTime; ULONG TitleIndex; ULONG ClassOffset;
         * ULONG ClassLength; ULONG NameLength; WCHAR Name[1]; */
        name_len_offset = 20;
        name_offset = 24;
        return true;
    default:
        return false;
    }
}

/**
 * @brief Copy a ULONG field inside a raw structure buffer.
 * @param[in, out] buffer The structure buffer.
 * @param[in] offset The byte offset of the field.
 * @param[in] value The value to store.
 */
static void StoreUlong(BYTE* buffer, ULONG offset, ULONG value)
{
    ULONG aligned = value;
    memcpy(buffer + offset, &aligned, sizeof(aligned));
}

/**
 * @brief Read a ULONG field inside a raw structure buffer.
 * @param[in] buffer The structure buffer.
 * @param[in] offset The byte offset of the field.
 * @return The value of the field.
 */
static ULONG LoadUlong(const BYTE* buffer, ULONG offset)
{
    ULONG value = 0;
    memcpy(&value, buffer + offset, sizeof(value));
    return value;
}

/**
 * @brief Answer a name information query with the key name translated into
 *        the view.
 *
 * The query runs into a local buffer first, so the translation does not depend
 * on the size of the caller buffer. The translated record is copied back when
 * it fits, otherwise STATUS_BUFFER_OVERFLOW reports the required size.
 *
 * @param[in] KeyHandle The hive key handle of the original call.
 * @param[in] KeyInformationClass The information class of the original call.
 * @param[out] KeyInformation The caller buffer, may be null.
 * @param[in] Length The size of the caller buffer.
 * @param[out] ResultLength The size of the translated record.
 * @return Status code.
 */
static NTSTATUS QueryKeyNameTranslated(HANDLE KeyHandle, KEY_INFORMATION_CLASS KeyInformationClass,
                                       PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    ULONG name_len_offset = 0;
    ULONG name_offset = 0;
    if (!NameLayoutOf(KeyInformationClass, name_len_offset, name_offset))
    {
        return sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
    }

    BYTE     local[kKeyNameBufferSize];
    ULONG    needed = 0;
    NTSTATUS st = sys_NtQueryKey(KeyHandle, KeyInformationClass, local, kKeyNameBufferSize, &needed);
    if (!NT_SUCCESS(st))
    {
        /* The name does not fit the local buffer or the query failed. */
        return sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
    }

    ULONG name_len = LoadUlong(local, name_len_offset);
    if (name_offset + name_len > needed)
    {
        /* Defensive: the record is shorter than the announced name. */
        return sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
    }

    std::wstring object_name;
    if (name_len > 0)
    {
        object_name.assign(reinterpret_cast<const WCHAR*>(local + name_offset), name_len / sizeof(WCHAR));
    }

    std::wstring view_name;
    if (!appbox::registry::Hive::TranslateHiveObjectName(object_name, view_name))
    {
        /* The name is not below the hive mount, return it unchanged. */
        if (ResultLength != nullptr)
        {
            *ResultLength = needed;
        }
        if (KeyInformation != nullptr && Length >= needed)
        {
            memcpy(KeyInformation, local, needed);
            return STATUS_SUCCESS;
        }
        return STATUS_BUFFER_OVERFLOW;
    }

    /*
     * Rebuild the record with the translated name. KeyNodeInformation carries
     * the optional class string behind the name, which moves along.
     */
    ULONG class_len = 0;
    ULONG class_offset = 0;
    if (KeyInformationClass == KeyNodeInformation)
    {
        class_offset = LoadUlong(local, 12); /* ClassOffset field. */
        class_len = LoadUlong(local, 16);    /* ClassLength field. */
        if (class_len == 0 || class_offset + class_len > needed)
        {
            class_offset = 0;
            class_len = 0;
        }
    }

    const ULONG new_name_len = (ULONG)(view_name.size() * sizeof(WCHAR));
    const ULONG new_needed = name_offset + new_name_len + class_len;

    if (ResultLength != nullptr)
    {
        *ResultLength = new_needed;
    }
    if (KeyInformation == nullptr || Length < new_needed)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    BYTE* out = reinterpret_cast<BYTE*>(KeyInformation);
    memcpy(out, local, name_offset);
    StoreUlong(out, name_len_offset, new_name_len);
    memcpy(out + name_offset, view_name.c_str(), new_name_len);

    if (class_len > 0)
    {
        memcpy(out + name_offset + new_name_len, local + class_offset, class_len);
        StoreUlong(out, 12, name_offset + new_name_len); /* ClassOffset field. */
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Correct the sub key and value counts of a forwarded full/cached
 *        information record to the merged three layer view.
 *
 * The counts of the layer of the handle are replaced by the counts of the
 * merged view, and the maximum length fields grow to the maximum of the layers
 * below, so a caller which allocates based on them can hold every merged entry.
 * The image and the real layer are filtered like the enumeration does, so the
 * counts never announce an entry which the enumeration hides.
 *
 * @param[in] KeyHandle The key handle of the original call.
 * @param[in] view The layer the handle points at.
 * @param[in] view_path The logical view path of the key.
 * @param[in] KeyInformationClass KeyFullInformation or KeyCachedInformation.
 * @param[out] KeyInformation The record which was filled by the forward call.
 * @return Status code of the forward call.
 */
static NTSTATUS FixMergedCounts(HANDLE KeyHandle, appbox::registry::HandleView view, const std::wstring& view_path,
                                KEY_INFORMATION_CLASS KeyInformationClass, PVOID KeyInformation, ULONG Length,
                                NTSTATUS forwarded)
{
    const bool full = KeyInformationClass == KeyFullInformation;
    /*
     * Both structures carry the count and maximum fields in their fixed head.
     * The record must hold the head, otherwise the fields were not written.
     */
    const ULONG head = full ? sizeof(KEY_FULL_INFORMATION) - sizeof(WCHAR) : sizeof(KEY_CACHED_INFORMATION);
    if (KeyInformation == nullptr || Length < head)
    {
        return forwarded;
    }

    std::wstring relative;
    const bool   mapped = appbox::registry::Hive::HiveRelativePath(view_path, relative);

    /*
     * The state hive layer: the handle of the caller when it points at that
     * layer, otherwise a handle opened here, so the counts of a handle of the
     * image report the entries of the layer above it as well.
     */
    HANDLE upper = view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr;
    HANDLE owned_upper = nullptr;
    if (upper == nullptr && mapped &&
        NT_SUCCESS(appbox::registry::Hive::OpenKey(relative, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                                                   OBJ_CASE_INSENSITIVE, nullptr, nullptr, &owned_upper)))
    {
        upper = owned_upper;
    }
    appbox::registry::KeyGuard upper_guard(owned_upper);

    std::vector<std::wstring> hive_sub_keys;
    std::vector<std::wstring> hive_values;
    if (upper != nullptr)
    {
        if (!NT_SUCCESS(appbox::registry::Hive::CollectSubKeyNames(upper, hive_sub_keys)) ||
            !NT_SUCCESS(appbox::registry::Hive::CollectValueNames(upper, hive_values)))
        {
            return forwarded;
        }
    }

    /* The image layer, filtered by the whiteouts of the state hive. */
    std::vector<std::wstring> image_sub_keys;
    std::vector<std::wstring> image_values;
    HANDLE                    image = nullptr;
    if (mapped &&
        NT_SUCCESS(appbox::registry::Hive::OpenImageKey(relative, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &image)))
    {
        appbox::registry::KeyGuard image_guard(image);
        if (NT_SUCCESS(appbox::registry::Hive::CollectSubKeyNames(image, image_sub_keys)) &&
            NT_SUCCESS(appbox::registry::Hive::CollectValueNames(image, image_values)))
        {
            appbox::registry::Hive::FilterHiddenImageEntries(view_path, false, image_sub_keys);
            appbox::registry::Hive::FilterHiddenImageEntries(view_path, true, image_values);
        }
        else
        {
            image_sub_keys.clear();
            image_values.clear();
        }
    }

    /* The real layer, filtered like the enumeration does. */
    std::vector<std::wstring> real_sub_keys;
    std::vector<std::wstring> real_values;
    HANDLE                    real = nullptr;
    if (!NT_SUCCESS(appbox::registry::Hive::OpenRealKey(view_path, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                                                        OBJ_CASE_INSENSITIVE, nullptr, nullptr, &real)))
    {
        /* Without a real layer the counts are the ones of the sandbox layers. */
        real = nullptr;
    }
    appbox::registry::KeyGuard real_guard(real);

    if (real != nullptr)
    {
        if (NT_SUCCESS(appbox::registry::Hive::CollectSubKeyNames(real, real_sub_keys)) &&
            NT_SUCCESS(appbox::registry::Hive::CollectValueNames(real, real_values)))
        {
            appbox::registry::Hive::FilterHiddenEntries(view_path, false, real_sub_keys);
            appbox::registry::Hive::FilterHiddenEntries(view_path, true, real_values);
        }
        else
        {
            real_sub_keys.clear();
            real_values.clear();
        }
    }

    /* The maximum length fields of the real layer, when they are available. */
    BYTE  real_buf[sizeof(KEY_FULL_INFORMATION) + 0x100];
    ULONG real_needed = 0;
    ULONG real_max_name_len = 0, real_max_class_len = 0;
    ULONG real_max_value_name_len = 0, real_max_value_data_len = 0;
    bool  have_real_max = false;
    if (real != nullptr &&
        NT_SUCCESS(sys_NtQueryKey(real, KeyFullInformation, real_buf, sizeof(real_buf), &real_needed)) &&
        real_needed >= sizeof(KEY_FULL_INFORMATION) - sizeof(WCHAR))
    {
        auto* info = reinterpret_cast<KEY_FULL_INFORMATION*>(real_buf);
        real_max_name_len = info->MaxNameLen;
        real_max_class_len = info->MaxClassLen;
        real_max_value_name_len = info->MaxValueNameLen;
        real_max_value_data_len = info->MaxValueDataLen;
        have_real_max = true;
    }

    const ULONG sub_keys = (ULONG)appbox::registry::CountMerged(hive_sub_keys, image_sub_keys, real_sub_keys);
    const ULONG values = (ULONG)appbox::registry::CountMerged(hive_values, image_values, real_values);

    if (full)
    {
        auto* info = reinterpret_cast<KEY_FULL_INFORMATION*>(KeyInformation);
        info->SubKeys = sub_keys;
        info->Values = values;
        if (have_real_max)
        {
            if (real_max_name_len > info->MaxNameLen)
            {
                info->MaxNameLen = real_max_name_len;
            }
            if (real_max_class_len > info->MaxClassLen)
            {
                info->MaxClassLen = real_max_class_len;
            }
            if (real_max_value_name_len > info->MaxValueNameLen)
            {
                info->MaxValueNameLen = real_max_value_name_len;
            }
            if (real_max_value_data_len > info->MaxValueDataLen)
            {
                info->MaxValueDataLen = real_max_value_data_len;
            }
        }
    }
    else
    {
        auto* info = reinterpret_cast<KEY_CACHED_INFORMATION*>(KeyInformation);
        info->SubKeys = sub_keys;
        info->Values = values;
        if (have_real_max)
        {
            if (real_max_name_len > info->MaxNameLen)
            {
                info->MaxNameLen = real_max_name_len;
            }
            if (real_max_value_name_len > info->MaxValueNameLen)
            {
                info->MaxValueNameLen = real_max_value_name_len;
            }
            if (real_max_value_data_len > info->MaxValueDataLen)
            {
                info->MaxValueDataLen = real_max_value_data_len;
            }
        }
    }

    return forwarded;
}

/**
 * @brief Detour of NtQueryKey().
 *
 * Key names reported for handles of a sandbox layer are translated back into
 * the logical view path (\REGISTRY\USER\<SID>\...), so a redirected handle
 * behaves exactly like the key it shadows, whatever layer of the sandbox it was
 * opened in. The sub key and value counts of the full and cached information
 * classes are corrected to the merged three layer view.
 */
static NTSTATUS NTAPI Hook_NtQueryKey(HANDLE KeyHandle, KEY_INFORMATION_CLASS KeyInformationClass, PVOID KeyInformation,
                                      ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (!appbox::registry::IsSandboxHandle(view))
    {
        return sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
    }

    switch (KeyInformationClass)
    {
    case KeyBasicInformation:
    case KeyNodeInformation:
    case KeyNameInformation:
        return QueryKeyNameTranslated(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);

    case KeyFullInformation:
    case KeyCachedInformation: {
        NTSTATUS st = sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
        if (!NT_SUCCESS(st))
        {
            return st;
        }
        /* The counts need the merged view even when the record is truncated. */
        return FixMergedCounts(KeyHandle, view, view_path, KeyInformationClass, KeyInformation, Length, st);
    }

    default:
        return sys_NtQueryKey(KeyHandle, KeyInformationClass, KeyInformation, Length, ResultLength);
    }
}

static void LoadNtQueryKey()
{
    sys_NtQueryKey = reinterpret_cast<T_NtQueryKey>(GetProcAddress(appbox::sys.h_ntdll, "NtQueryKey"));
}

appbox::HookRecord appbox::HookNtQueryKey = {
    "NtQueryKey",
    LoadNtQueryKey,
    (void**)&sys_NtQueryKey,
    Hook_NtQueryKey,
};
