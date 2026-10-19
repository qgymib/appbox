#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "registry/KeyGuard.hpp"
#include "utils/Log.hpp"
#include "NtQueryValueKey.hpp"

T_NtQueryValueKey sys_NtQueryValueKey = nullptr;

static nlohmann::json NtQueryValueKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                                              KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                              PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["ValueName"] = appbox::ToJson(ValueName);
    json["KeyValueInformationClass"] = KeyValueInformationClass;
    json["KeyValueInformation"] = appbox::PointerToString(KeyValueInformation);
    json["Length"] = Length;
    json["ResultLength"] = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtQueryValueKey", NtQueryValueKeyLogParam);

/**
 * @brief Detour of NtQueryValueKey().
 *
 * A value query below a handle of a sandbox layer answers with the value of the
 * highest layer which holds it: the state hive wins over the image, which wins
 * over the real registry. The handle of the caller answers for its own layer,
 * so a handle of the image asks the state hive first, and a handle of the state
 * hive asks the image when the value does not exist there.
 *
 * A value which the isolation hides or which the sandbox deleted (a whiteout)
 * is reported as missing, whatever the layers below hold: the marker of the
 * state hive hides the value of the image and of the host alike. Buffer related
 * results of a layer are returned as they are: that layer holds the value, only
 * the caller buffer is too small.
 *
 * Handles which do not point into a sandbox layer are forwarded unchanged.
 */
static NTSTATUS NTAPI Hook_NtQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                                           KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                           PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (!appbox::registry::IsSandboxHandle(view))
    {
        return sys_NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length,
                                   ResultLength);
    }

    std::wstring relative;
    std::wstring value_name;
    const bool   mapped = appbox::registry::Hive::HiveRelativePath(view_path, relative) &&
                          appbox::registry::ReadValueName(ValueName, value_name);
    const bool   whited_out = mapped && appbox::registry::Hive::IsValueWhitedOut(relative, value_name);

    /*
     * The state hive is the layer above the image: a handle of the image asks
     * that layer first, so the value the sandboxed process wrote wins over the
     * value of the image.
     */
    if (view == appbox::registry::HandleView::ImageHandle && mapped)
    {
        HANDLE upper = nullptr;
        if (NT_SUCCESS(appbox::registry::Hive::OpenKey(relative, KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr,
                                                       nullptr, &upper)))
        {
            appbox::registry::KeyGuard guard(upper);
            const NTSTATUS             st_upper = sys_NtQueryValueKey(upper, ValueName, KeyValueInformationClass,
                                                                      KeyValueInformation, Length, ResultLength);
            if (st_upper != STATUS_OBJECT_NAME_NOT_FOUND)
            {
                return st_upper;
            }
        }
    }

    /*
     * The layer of the handle answers next. A handle of the state hive always
     * answers, because a value the sandbox wrote after the delete of the value
     * is the value of the view; a handle of the image is a layer below the state
     * hive, so the marker of a deleted value hides its answer as well.
     */
    NTSTATUS st = STATUS_OBJECT_NAME_NOT_FOUND;
    if (view == appbox::registry::HandleView::HiveHandle || !whited_out)
    {
        st = sys_NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length,
                                 ResultLength);
        if (st != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            return st;
        }
    }

    if (!mapped || whited_out)
    {
        /* The sandbox deleted the value: no layer below holds it in the view. */
        return st;
    }

    /* The image is the layer below a handle of the state hive. */
    if (view == appbox::registry::HandleView::HiveHandle)
    {
        HANDLE image = nullptr;
        if (NT_SUCCESS(appbox::registry::Hive::OpenImageKey(relative, KEY_QUERY_VALUE, &image)))
        {
            appbox::registry::KeyGuard guard(image);
            const NTSTATUS             st_image = sys_NtQueryValueKey(image, ValueName, KeyValueInformationClass,
                                                                      KeyValueInformation, Length, ResultLength);
            if (st_image != STATUS_OBJECT_NAME_NOT_FOUND)
            {
                return st_image;
            }
        }
    }

    /* Read through: the value is missing in the sandbox layers, try the host. */
    if (appbox::registry::Hive::HidesHostValue(relative, value_name))
    {
        return st;
    }

    HANDLE real = nullptr;
    if (!NT_SUCCESS(appbox::registry::Hive::OpenRealKey(view_path, KEY_QUERY_VALUE, OBJ_CASE_INSENSITIVE, nullptr,
                                                        nullptr, &real)))
    {
        return st;
    }

    appbox::registry::KeyGuard real_guard(real);
    return sys_NtQueryValueKey(real, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
}

static void LoadNtQueryValueKey()
{
    sys_NtQueryValueKey = reinterpret_cast<T_NtQueryValueKey>(GetProcAddress(appbox::sys.h_ntdll, "NtQueryValueKey"));
}

appbox::HookRecord appbox::HookNtQueryValueKey = {
    "NtQueryValueKey",
    LoadNtQueryValueKey,
    (void**)&sys_NtQueryValueKey,
    Hook_NtQueryValueKey,
};
