#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/FontApi.hpp"
#include "utils/Log.hpp"
#include "NtGdiRemoveFontResourceW.hpp"

static nlohmann::json NtGdiRemoveFontResourceWLogParam(LPCWSTR pwszFiles, ULONG cwc, ULONG cFiles, ULONG flags)
{
    nlohmann::json json;
    json["pwszFiles"] = appbox::PointerToString(pwszFiles);
    json["cwc"] = cwc;
    json["cFiles"] = cFiles;
    json["flags"] = flags;
    return json;
}

T_NtGdiRemoveFontResourceW sys_NtGdiRemoveFontResourceW = nullptr;
static appbox::LoggerF     logger("NtGdiRemoveFontResourceW", NtGdiRemoveFontResourceWLogParam);

static int WINAPI Hook_NtGdiRemoveFontResourceW(LPCWSTR pwszFiles, ULONG cwc, ULONG cFiles, ULONG flags,
                                                ULONG_PTR reserved, PVOID pdv)
{
    logger.Log(pwszFiles, cwc, cFiles, flags);

    /*
     * The removal is rewritten like the addition, so a caller which adds a font
     * of the view and removes it again with the same path reaches the same
     * file. Only a buffer which names one file is rewritten, see
     * docs/FontsIsolation.md.
     */
    std::wstring rewritten;
    ULONG        rewritten_cwc = 0;
    if (cFiles == 1 && appbox::fonts::RewriteFontFileBuffer(pwszFiles, cwc, rewritten, rewritten_cwc))
    {
        LOG_D(L"font resource: {}", rewritten.c_str());
        return sys_NtGdiRemoveFontResourceW(rewritten.c_str(), rewritten_cwc, cFiles, flags, reserved, pdv);
    }

    return sys_NtGdiRemoveFontResourceW(pwszFiles, cwc, cFiles, flags, reserved, pdv);
}

static void LoadNtGdiRemoveFontResourceW()
{
    sys_NtGdiRemoveFontResourceW =
        reinterpret_cast<T_NtGdiRemoveFontResourceW>(GetProcAddress(appbox::sys.h_win32u, "NtGdiRemoveFontResourceW"));
}

appbox::HookRecord appbox::HookNtGdiRemoveFontResourceW = {
    "NtGdiRemoveFontResourceW",
    LoadNtGdiRemoveFontResourceW,
    (void**)&sys_NtGdiRemoveFontResourceW,
    Hook_NtGdiRemoveFontResourceW,
};
