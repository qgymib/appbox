#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtCompressKey.hpp"

T_NtCompressKey sys_NtCompressKey = nullptr;

static nlohmann::json NtCompressKeyLogParam(HANDLE Key)
{
    nlohmann::json json;
    json["Key"] = appbox::PointerToString(Key);
    return json;
}

static appbox::LoggerF logger("NtCompressKey", NtCompressKeyLogParam);

/**
 * @brief Detour of NtCompressKey().
 *
 * The compress acts on the hive file which holds the key, so the layer of the
 * handle decides the answer: the hive is the state of the sandbox, so a
 * compress of a key of the hive acts on a file of the sandbox and the call is
 * forwarded (the kernel asks for a privilege the caller may not hold, which is
 * its own answer), while the hive of the host is not the state of the sandbox,
 * so the call is refused. A handle outside the root keys of the view belongs to
 * no layer of the view and is forwarded unchanged.
 */
static NTSTATUS Hook_NtCompressKey(HANDLE Key)
{
    logger.Log(Key);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(Key, view_path);
    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    return sys_NtCompressKey(Key);
}

static void LoadNtCompressKey()
{
    sys_NtCompressKey = reinterpret_cast<T_NtCompressKey>(GetProcAddress(appbox::sys.h_ntdll, "NtCompressKey"));
}

appbox::HookRecord appbox::HookNtCompressKey = {
    "NtCompressKey",
    LoadNtCompressKey,
    (void**)&sys_NtCompressKey,
    Hook_NtCompressKey,
};
