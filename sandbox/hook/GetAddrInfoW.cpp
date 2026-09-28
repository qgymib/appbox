#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "GetAddrInfoW.hpp"
#include <string>

T_GetAddrInfoW sys_GetAddrInfoW = nullptr;

static nlohmann::json GetAddrInfoWLogParam(PCWSTR NodeName, PCWSTR ServiceName, const ADDRINFOW* Hints,
                                           const std::string& redirect)
{
    nlohmann::json param;
    param["NodeName"] = appbox::network::WideNameToUTF8(NodeName);
    param["ServiceName"] = appbox::network::WideNameToUTF8(ServiceName);
    param["Family"] = Hints != nullptr ? Hints->ai_family : AF_UNSPEC;
    param["Redirect"] = redirect;
    return param;
}

static appbox::LoggerF logger("GetAddrInfoW", GetAddrInfoWLogParam);

/**
 * @brief Copy hints of a resolution and force the name to be an address.
 *
 * The name handed to the original call is the address the hostname is
 * redirected to, so the resolution has to read it as an address literal and
 * must not ask the host for it.
 *
 * @param[in] hints Hints of the caller, may be null.
 * @param[out] storage Storage of the copied hints.
 * @return The hints to pass to the original call.
 */
static const ADDRINFOW* NumericHints(const ADDRINFOW* hints, ADDRINFOW& storage)
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
 * @brief Detour of GetAddrInfoW().
 *
 * A hostname the isolation file redirects is resolved by handing the redirect
 * address to the original entry point as the name, together with the flag which
 * forbids a name resolution; every other name keeps the resolution of the host.
 */
static INT WSAAPI Hook_GetAddrInfoW(PCWSTR NodeName, PCWSTR ServiceName, const ADDRINFOW* Hints, PADDRINFOW* Result)
{
    const std::string redirect =
        appbox::network::FindRedirect(appbox::network::WideNameToUTF8(NodeName),
                                      appbox::network::FamilyOf(Hints != nullptr ? Hints->ai_family : AF_UNSPEC));
    logger.Log(NodeName, ServiceName, Hints, redirect);

    const std::wstring target = appbox::network::RedirectToWide(redirect);
    if (target.empty())
    {
        return sys_GetAddrInfoW(NodeName, ServiceName, Hints, Result);
    }

    ADDRINFOW storage;
    return sys_GetAddrInfoW(target.c_str(), ServiceName, NumericHints(Hints, storage), Result);
}

static void LoadGetAddrInfoW()
{
    sys_GetAddrInfoW = reinterpret_cast<T_GetAddrInfoW>(GetProcAddress(appbox::sys.h_ws2_32, "GetAddrInfoW"));
}

appbox::HookRecord appbox::HookGetAddrInfoW = {
    "GetAddrInfoW",
    LoadGetAddrInfoW,
    (void**)&sys_GetAddrInfoW,
    Hook_GetAddrInfoW,
};
