#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "getaddrinfo.hpp"
#include <string>

T_getaddrinfo sys_getaddrinfo = nullptr;

static nlohmann::json GetAddrInfoLogParam(PCSTR NodeName, PCSTR ServiceName, const ADDRINFOA* Hints,
                                          const std::string& redirect)
{
    nlohmann::json param;
    param["NodeName"] = appbox::network::AnsiNameToUTF8(NodeName);
    param["ServiceName"] = appbox::network::AnsiNameToUTF8(ServiceName);
    param["Family"] = Hints != nullptr ? Hints->ai_family : AF_UNSPEC;
    param["Redirect"] = redirect;
    return param;
}

static appbox::LoggerF logger("getaddrinfo", GetAddrInfoLogParam);

/**
 * @brief Copy hints of a narrow resolution and force an address literal.
 * @param[in] hints Hints of the caller, may be null.
 * @param[out] storage Storage of the copied hints.
 * @return The hints to pass to the original call.
 */
static const ADDRINFOA* NumericHints(const ADDRINFOA* hints, ADDRINFOA& storage)
{
    if (hints != nullptr)
    {
        storage = *hints;
    }
    else
    {
        ZeroMemory(&storage, sizeof(storage));
        storage.ai_family = AF_UNSPEC;
    }
    storage.ai_flags |= AI_NUMERICHOST;
    return &storage;
}

/**
 * @brief Detour of getaddrinfo(), the ANSI variant of GetAddrInfoW().
 *
 * A hostname the isolation file redirects is resolved by handing the redirect
 * address to the original entry point as the name, together with the flag which
 * forbids a name resolution; every other name keeps the resolution of the host.
 */
static INT WSAAPI Hook_getaddrinfo(PCSTR NodeName, PCSTR ServiceName, const ADDRINFOA* Hints, PADDRINFOA* Result)
{
    const std::string redirect =
        appbox::network::FindRedirect(appbox::network::AnsiNameToUTF8(NodeName),
                                      appbox::network::FamilyOf(Hints != nullptr ? Hints->ai_family : AF_UNSPEC));
    logger.Log(NodeName, ServiceName, Hints, redirect);

    if (redirect.empty())
    {
        return sys_getaddrinfo(NodeName, ServiceName, Hints, Result);
    }

    ADDRINFOA storage;
    return sys_getaddrinfo(redirect.c_str(), ServiceName, NumericHints(Hints, storage), Result);
}

static void LoadGetAddrInfo()
{
    sys_getaddrinfo = reinterpret_cast<T_getaddrinfo>(GetProcAddress(appbox::sys.h_ws2_32, "getaddrinfo"));
}

appbox::HookRecord appbox::HookGetAddrInfo = {
    "getaddrinfo",
    LoadGetAddrInfo,
    (void**)&sys_getaddrinfo,
    Hook_getaddrinfo,
};
