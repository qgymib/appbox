#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "gethostbyname.hpp"
#include <string>

T_gethostbyname sys_gethostbyname = nullptr;

static nlohmann::json GetHostByNameLogParam(PCSTR Name, const std::string& redirect)
{
    nlohmann::json param;
    param["Name"] = appbox::network::AnsiNameToUTF8(Name);
    param["Redirect"] = redirect;
    return param;
}

static appbox::LoggerF logger("gethostbyname", GetHostByNameLogParam);

/**
 * @brief Detour of gethostbyname(), the legacy name resolution of winsock.
 *
 * The legacy resolution answers IPv4 addresses only, so a redirection is looked
 * up for that family and the redirect address is handed to the original entry
 * point as the name to resolve.
 */
static struct hostent* WSAAPI Hook_gethostbyname(PCSTR Name)
{
    const std::string redirect =
        appbox::network::FindRedirect(appbox::network::AnsiNameToUTF8(Name), appbox::network::RequestedFamily::IPv4);
    logger.Log(Name, redirect);

    if (redirect.empty())
    {
        return sys_gethostbyname(Name);
    }
    return sys_gethostbyname(redirect.c_str());
}

static void LoadGetHostByName()
{
    sys_gethostbyname = reinterpret_cast<T_gethostbyname>(GetProcAddress(appbox::sys.h_ws2_32, "gethostbyname"));
}

appbox::HookRecord appbox::HookGetHostByName = {
    "gethostbyname",
    LoadGetHostByName,
    (void**)&sys_gethostbyname,
    Hook_gethostbyname,
};
