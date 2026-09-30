#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/FontApi.hpp"
#include "utils/Log.hpp"
#include "NtGdiAddFontResourceW.hpp"

static nlohmann::json NtGdiAddFontResourceWLogParam(LPCWSTR pwszFiles, ULONG cwc, ULONG cFiles, ULONG flags)
{
    nlohmann::json json;
    json["pwszFiles"] = appbox::PointerToString(pwszFiles);
    json["cwc"] = cwc;
    json["cFiles"] = cFiles;
    json["flags"] = flags;
    return json;
}

T_NtGdiAddFontResourceW sys_NtGdiAddFontResourceW = nullptr;
static appbox::LoggerF  logger("NtGdiAddFontResourceW", NtGdiAddFontResourceWLogParam);

static int WINAPI Hook_NtGdiAddFontResourceW(LPCWSTR pwszFiles, ULONG cwc, ULONG cFiles, ULONG flags,
                                             ULONG_PTR reserved, PVOID pdv)
{
    logger.Log(pwszFiles, cwc, cFiles, flags);

    /*
     * Only a buffer which names one file is rewritten: this build refuses a
     * buffer which carries several files, so the layout of such a buffer cannot
     * be rebuilt with confidence and the call is forwarded unchanged. See
     * docs/FontsIsolation.md for the measurement.
     */
    std::wstring rewritten;
    ULONG        rewritten_cwc = 0;
    if (cFiles == 1 && appbox::fonts::RewriteFontFileBuffer(pwszFiles, cwc, rewritten, rewritten_cwc))
    {
        /*
         * The font driver of the system opens the file of a font resource
         * without passing the hooks of the sandbox, so the path has to name the
         * file of the layer the view shows.
         */
        LOG_D(L"font resource: {}", rewritten.c_str());
        return sys_NtGdiAddFontResourceW(rewritten.c_str(), rewritten_cwc, cFiles, flags, reserved, pdv);
    }

    return sys_NtGdiAddFontResourceW(pwszFiles, cwc, cFiles, flags, reserved, pdv);
}

static void LoadNtGdiAddFontResourceW()
{
    sys_NtGdiAddFontResourceW =
        reinterpret_cast<T_NtGdiAddFontResourceW>(GetProcAddress(appbox::sys.h_win32u, "NtGdiAddFontResourceW"));
}

appbox::HookRecord appbox::HookNtGdiAddFontResourceW = {
    "NtGdiAddFontResourceW",
    LoadNtGdiAddFontResourceW,
    (void**)&sys_NtGdiAddFontResourceW,
    Hook_NtGdiAddFontResourceW,
};
