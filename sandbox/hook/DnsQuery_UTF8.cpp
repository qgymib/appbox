#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "DnsQuery_UTF8.hpp"
#include <string>

T_DnsQuery_UTF8 sys_DnsQuery_UTF8 = nullptr;

static appbox::LoggerF logger("DnsQuery_UTF8", appbox::network::DnsQueryLogParam);

/**
 * @brief Detour of DnsQuery_UTF8(), the UTF-8 entry point of the DNS client.
 *
 * Only a question for `A`, `AAAA` or `ANY` is redirected, because the entry of
 * the isolation file holds an address. A hit is answered by handing the redirect
 * address to the original entry point as the name, together with the flag which
 * forbids a query on the wire.
 */
static LONG WSAAPI Hook_DnsQuery_UTF8(PCSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results,
                                      PVOID* Reserved)
{
    appbox::network::RequestedFamily family = appbox::network::RequestedFamily::Any;
    const std::string                name = appbox::network::AnsiNameToUTF8(Name);
    const std::string                redirect =
        appbox::network::FamilyOfQueryType(Type, family) ? appbox::network::FindRedirect(name, family) : std::string();
    logger.Log(name, Type, Options, redirect);

    if (redirect.empty())
    {
        return sys_DnsQuery_UTF8(Name, Type, Options, Extra, Results, Reserved);
    }
    return sys_DnsQuery_UTF8(redirect.c_str(), Type, Options | DNS_QUERY_NO_WIRE_QUERY, Extra, Results, Reserved);
}

static void LoadDnsQueryUTF8()
{
    sys_DnsQuery_UTF8 = reinterpret_cast<T_DnsQuery_UTF8>(GetProcAddress(appbox::sys.h_dnsapi, "DnsQuery_UTF8"));
}

appbox::HookRecord appbox::HookDnsQueryUTF8 = {
    "DnsQuery_UTF8",
    LoadDnsQueryUTF8,
    (void**)&sys_DnsQuery_UTF8,
    Hook_DnsQuery_UTF8,
};
