#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "GetAddrInfoExW.hpp"
#include <string>

T_GetAddrInfoExW sys_GetAddrInfoExW = nullptr;

static nlohmann::json GetAddrInfoExWLogParam(PCWSTR NodeName, PCWSTR ServiceName, DWORD NameSpace,
                                             const ADDRINFOEXW* Hints, const std::string& redirect)
{
    nlohmann::json param;
    param["NodeName"] = appbox::network::WideNameToUTF8(NodeName);
    param["ServiceName"] = appbox::network::WideNameToUTF8(ServiceName);
    param["NameSpace"] = NameSpace;
    param["Family"] = Hints != nullptr ? Hints->ai_family : AF_UNSPEC;
    param["Redirect"] = redirect;
    return param;
}

static appbox::LoggerF logger("GetAddrInfoExW", GetAddrInfoExWLogParam);

/**
 * @brief Copy hints of an extended resolution and force an address literal.
 * @param[in] hints Hints of the caller, may be null.
 * @param[out] storage Storage of the copied hints.
 * @return The hints to pass to the original call.
 */
static const ADDRINFOEXW* NumericHints(const ADDRINFOEXW* hints, ADDRINFOEXW& storage)
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
 * @brief Detour of GetAddrInfoExW().
 *
 * Only the plain synchronous query is redirected: a query of another name
 * space, an asynchronous query and a query which continues in an overlapped
 * operation keep the resolution of the host, because the redirection cannot
 * drive the completion of such a call.
 */
static INT WSAAPI Hook_GetAddrInfoExW(PCWSTR NodeName, PCWSTR ServiceName, DWORD NameSpace, LPGUID Provider,
                                      const ADDRINFOEXW* Hints, PADDRINFOEXW* Result, struct timeval* Timeout,
                                      LPOVERLAPPED Overlapped, PVOID CompletionRoutine, LPHANDLE NameHandle)
{
    const bool redirectable = (NameSpace == NS_ALL || NameSpace == NS_DNS) && Overlapped == nullptr &&
                              CompletionRoutine == nullptr && NameHandle == nullptr;

    std::string redirect;
    if (redirectable)
    {
        redirect =
            appbox::network::FindRedirect(appbox::network::WideNameToUTF8(NodeName),
                                          appbox::network::FamilyOf(Hints != nullptr ? Hints->ai_family : AF_UNSPEC));
    }
    logger.Log(NodeName, ServiceName, NameSpace, Hints, redirect);

    const std::wstring target = appbox::network::RedirectToWide(redirect);
    if (target.empty())
    {
        return sys_GetAddrInfoExW(NodeName, ServiceName, NameSpace, Provider, Hints, Result, Timeout, Overlapped,
                                  CompletionRoutine, NameHandle);
    }

    ADDRINFOEXW storage;
    return sys_GetAddrInfoExW(target.c_str(), ServiceName, NameSpace, Provider, NumericHints(Hints, storage), Result,
                              Timeout, Overlapped, CompletionRoutine, NameHandle);
}

static void LoadGetAddrInfoExW()
{
    sys_GetAddrInfoExW = reinterpret_cast<T_GetAddrInfoExW>(GetProcAddress(appbox::sys.h_ws2_32, "GetAddrInfoExW"));
}

appbox::HookRecord appbox::HookGetAddrInfoExW = {
    "GetAddrInfoExW",
    LoadGetAddrInfoExW,
    (void**)&sys_GetAddrInfoExW,
    Hook_GetAddrInfoExW,
};
