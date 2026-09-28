#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "ResolveName.hpp"
#include "WString.hpp"
#include <iterator>
#include <string>
#include <vector>

namespace
{

/*
 * The name resolution APIs of the application, which the sandbox hooks. The
 * winsock 2 and the DNS headers cannot be included next to the winsock header
 * of <windows.h>, so the entry points and the structures the probe reads are
 * declared here.
 */
extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfow
 */
INT WSAAPI GetAddrInfoW(PCWSTR NodeName, PCWSTR ServiceName, const ADDRINFOW* Hints, PADDRINFOW* Result);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-freeaddrinfow
 */
void WSAAPI FreeAddrInfoW(PADDRINFOW Result);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-inetntopw
 */
PCWSTR WSAAPI InetNtopW(INT Family, const VOID* Address, PWSTR Buffer, SIZE_T BufferSize);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_utf8
 */
LONG WSAAPI DnsQuery_UTF8(PCSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results, PVOID* Reserved);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsrecordlistfree
 */
void WSAAPI DnsRecordListFree(PVOID RecordList, INT FreeType);
}

/**
 * @brief The header of a DNS record the DNS API returns.
 *
 * The data of a record follows the header; the probe reads the address of an A
 * and of an AAAA record from there.
 *
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/ns-windns-dns_recordw
 */
struct DnsRecordHeader
{
    struct DnsRecordHeader* pNext;
    PVOID                   pszName;
    WORD                    wType;
    WORD                    wDataLength;
    DWORD                   Flags;
    DWORD                   dwTtl;
    DWORD                   dwReserved;
};

/** Offset of the address inside a DNS record, which is aligned to eight bytes. */
constexpr std::size_t kDnsRecordDataOffset = 32;

/** Offset of the address inside an IPv4 socket address. */
constexpr std::size_t kIpv4AddressOffset = 4;

/** Offset of the address inside an IPv6 socket address. */
constexpr std::size_t kIpv6AddressOffset = 8;

/** Number of records a probe reads from a result, which bounds the walk. */
constexpr int kMaxRecords = 16;

/**
 * @brief Format the raw bytes of an address.
 * @param[in] family Family of the address.
 * @param[in] address Bytes of the address.
 * @return The address literal, empty when the family is not supported.
 */
std::string FormatAddress(int family, const void* address)
{
    if (family != AF_INET && family != AF_INET6)
    {
        return std::string();
    }

    wchar_t    buffer[64] = {};
    const auto text = InetNtopW(family, address, buffer, std::size(buffer));
    if (text == nullptr)
    {
        return std::string();
    }
    return appbox::WideToUTF8(text);
}

/**
 * @brief Format the address of a socket address.
 * @param[in] family Family of the socket address.
 * @param[in] socket_address The socket address.
 * @return The address literal, empty when the family is not supported.
 */
std::string FormatSocketAddress(int family, const void* socket_address)
{
    const auto* bytes = static_cast<const unsigned char*>(socket_address);
    if (family == AF_INET)
    {
        return FormatAddress(family, bytes + kIpv4AddressOffset);
    }
    if (family == AF_INET6)
    {
        return FormatAddress(family, bytes + kIpv6AddressOffset);
    }
    return std::string();
}

/**
 * @brief Resolve a hostname with GetAddrInfoW.
 * @param[in] name Hostname to resolve, UTF-8.
 * @param[in] family Address family to ask for.
 * @param[out] rsp Result of the call.
 */
void ResolveWithGetAddrInfo(const std::string& name, int family, appbox::test::ProtocolResolveName::Rsp& rsp)
{
    ADDRINFOW hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = family;

    PADDRINFOW result = nullptr;
    const auto text = appbox::UTF8ToWide(name);
    rsp.code = GetAddrInfoW(text.c_str(), nullptr, &hints, &result);

    for (auto* node = result; node != nullptr && static_cast<int>(rsp.addresses.size()) < kMaxRecords;
         node = node->ai_next)
    {
        const auto address = FormatSocketAddress(node->ai_family, node->ai_addr);
        if (!address.empty())
        {
            rsp.addresses.push_back(address);
        }
    }

    if (result != nullptr)
    {
        FreeAddrInfoW(result);
    }
}

/**
 * @brief Resolve a hostname with DnsQuery_UTF8.
 * @param[in] name Hostname to resolve, UTF-8.
 * @param[in] type Record type to ask for.
 * @param[out] rsp Result of the call.
 */
void ResolveWithDnsQuery(const std::string& name, WORD type, appbox::test::ProtocolResolveName::Rsp& rsp)
{
    PVOID records = nullptr;
    rsp.code = static_cast<int>(DnsQuery_UTF8(name.c_str(), type, 0, nullptr, &records, nullptr));

    auto* node = static_cast<DnsRecordHeader*>(records);
    for (int index = 0; node != nullptr && index < kMaxRecords; ++index)
    {
        const auto* base = reinterpret_cast<const unsigned char*>(node);
        if (node->wType == DNS_TYPE_A)
        {
            rsp.addresses.push_back(FormatAddress(AF_INET, base + kDnsRecordDataOffset));
        }
        else if (node->wType == DNS_TYPE_AAAA)
        {
            rsp.addresses.push_back(FormatAddress(AF_INET6, base + kDnsRecordDataOffset));
        }
        node = node->pNext;
    }

    if (records != nullptr)
    {
        DnsRecordListFree(records, 0);
    }
}

} // namespace

static nlohmann::json ProbeResolveName_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolResolveName::Req>();

    appbox::test::ProtocolResolveName::Rsp rsp;
    if (req.api == appbox::test::ProtocolResolveName::Req::Api::DnsQuery)
    {
        ResolveWithDnsQuery(req.name, static_cast<WORD>(req.type), rsp);
    }
    else
    {
        ResolveWithGetAddrInfo(req.name, req.family, rsp);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeResolveName("ResolveName", ProbeResolveName_Entry);
