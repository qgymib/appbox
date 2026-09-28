#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "network/DnsTable.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "NameResolution.hpp"
#include <string>

std::string appbox::network::WideNameToUTF8(PCWSTR name)
{
    if (name == nullptr)
    {
        return std::string();
    }

    UNICODE_STRING text;
    sys_RtlInitUnicodeString(&text, name);
    return appbox::UnicodeStringToUTF8(&text);
}

std::string appbox::network::AnsiNameToUTF8(PCSTR name)
{
    if (name == nullptr)
    {
        return std::string();
    }

    const int length = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    if (length <= 1)
    {
        return std::string();
    }

    std::wstring text(static_cast<std::size_t>(length - 1), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, name, -1, text.data(), length) != length)
    {
        return std::string();
    }

    try
    {
        return appbox::WideToUTF8(text);
    }
    catch (...)
    {
        return std::string();
    }
}

std::wstring appbox::network::RedirectToWide(const std::string& redirect)
{
    try
    {
        return appbox::UTF8ToWide(redirect);
    }
    catch (...)
    {
        return std::wstring();
    }
}

appbox::network::RequestedFamily appbox::network::FamilyOf(int family)
{
    switch (family)
    {
    case AF_INET:
        return RequestedFamily::IPv4;
    case AF_INET6:
        return RequestedFamily::IPv6;
    default:
        return RequestedFamily::Any;
    }
}

bool appbox::network::FamilyOfQueryType(WORD type, RequestedFamily& family)
{
    switch (type)
    {
    case DNS_TYPE_A:
        family = RequestedFamily::IPv4;
        return true;
    case DNS_TYPE_AAAA:
        family = RequestedFamily::IPv6;
        return true;
    case DNS_TYPE_ANY:
        family = RequestedFamily::Any;
        return true;
    default:
        return false;
    }
}

std::string appbox::network::FindRedirect(const std::string& hostname, RequestedFamily family)
{
    if (hostname.empty() || appbox::sandbox == nullptr)
    {
        return std::string();
    }

    const DnsEntry* entry = appbox::sandbox->dns_table.Find(hostname, family);
    if (entry == nullptr)
    {
        return std::string();
    }
    return entry->redirect;
}

nlohmann::json appbox::network::DnsQueryLogParam(const std::string& name, WORD type, DWORD options,
                                                 const std::string& redirect)
{
    nlohmann::json param;
    param["Name"] = name;
    param["Type"] = type;
    param["Options"] = options;
    param["Redirect"] = redirect;
    return param;
}

bool appbox::network::LoadNameResolutionModules()
{
    if (appbox::sys.h_ws2_32 == nullptr)
    {
        appbox::sys.h_ws2_32 = LoadLibraryW(L"ws2_32.dll");
    }
    if (appbox::sys.h_dnsapi == nullptr)
    {
        appbox::sys.h_dnsapi = LoadLibraryW(L"dnsapi.dll");
    }

    return appbox::sys.h_ws2_32 != nullptr && appbox::sys.h_dnsapi != nullptr;
}
