#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "DnsQuery_W.hpp"
#include <string>

T_DnsQuery_W sys_DnsQuery_W = nullptr;

static appbox::LoggerF logger("DnsQuery_W", appbox::network::DnsQueryLogParam);

/**
 * @brief Detour of DnsQuery_W(), the wide entry point of the DNS client.
 *
 * Only a question for `A`, `AAAA` or `ANY` is redirected, because the entry of
 * the isolation file holds an address. A hit is answered by handing the redirect
 * address to the original entry point as the name, together with the flag which
 * forbids a query on the wire; a redirect which cannot be converted is not
 * answered.
 */
static LONG WSAAPI Hook_DnsQuery_W(PCWSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results, PVOID* Reserved)
{
    appbox::network::RequestedFamily family = appbox::network::RequestedFamily::Any;
    const std::string                name = appbox::network::WideNameToUTF8(Name);
    const std::string                redirect =
        appbox::network::FamilyOfQueryType(Type, family) ? appbox::network::FindRedirect(name, family) : std::string();
    logger.Log(name, Type, Options, redirect);

    if (redirect.empty())
    {
        return sys_DnsQuery_W(Name, Type, Options, Extra, Results, Reserved);
    }

    const std::wstring target = appbox::network::RedirectToWide(redirect);
    if (target.empty())
    {
        return sys_DnsQuery_W(Name, Type, Options, Extra, Results, Reserved);
    }
    return sys_DnsQuery_W(target.c_str(), Type, Options | DNS_QUERY_NO_WIRE_QUERY, Extra, Results, Reserved);
}

static void LoadDnsQueryW()
{
    sys_DnsQuery_W = reinterpret_cast<T_DnsQuery_W>(GetProcAddress(appbox::sys.h_dnsapi, "DnsQuery_W"));
}

appbox::HookRecord appbox::HookDnsQueryW = {
    "DnsQuery_W",
    LoadDnsQueryW,
    (void**)&sys_DnsQuery_W,
    Hook_DnsQuery_W,
};
