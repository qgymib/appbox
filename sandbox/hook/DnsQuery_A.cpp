#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/NameResolution.hpp"
#include "DnsQuery_A.hpp"
#include <string>

T_DnsQuery_A sys_DnsQuery_A = nullptr;

static appbox::LoggerF logger("DnsQuery_A", appbox::network::DnsQueryLogParam);

/**
 * @brief Detour of DnsQuery_A(), the ANSI entry point of the DNS client.
 *
 * Only a question for `A`, `AAAA` or `ANY` is redirected, because the entry of
 * the isolation file holds an address. A hit is answered by handing the redirect
 * address to the original entry point as the name, together with the flag which
 * forbids a query on the wire.
 */
static LONG WSAAPI Hook_DnsQuery_A(PCSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results, PVOID* Reserved)
{
    appbox::network::RequestedFamily family = appbox::network::RequestedFamily::Any;
    const std::string                name = appbox::network::AnsiNameToUTF8(Name);
    const std::string                redirect =
        appbox::network::FamilyOfQueryType(Type, family) ? appbox::network::FindRedirect(name, family) : std::string();
    logger.Log(name, Type, Options, redirect);

    if (redirect.empty())
    {
        return sys_DnsQuery_A(Name, Type, Options, Extra, Results, Reserved);
    }
    return sys_DnsQuery_A(redirect.c_str(), Type, Options | DNS_QUERY_NO_WIRE_QUERY, Extra, Results, Reserved);
}

static void LoadDnsQueryA()
{
    sys_DnsQuery_A = reinterpret_cast<T_DnsQuery_A>(GetProcAddress(appbox::sys.h_dnsapi, "DnsQuery_A"));
}

appbox::HookRecord appbox::HookDnsQueryA = {
    "DnsQuery_A",
    LoadDnsQueryA,
    (void**)&sys_DnsQuery_A,
    Hook_DnsQuery_A,
};
