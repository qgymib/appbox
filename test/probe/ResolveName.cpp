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
 *
 * gethostbyname() is the exception: the legacy winsock header of <windows.h>
 * declares it already, so it is only declared when that header is not part of
 * the translation unit.
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
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo
 */
INT WSAAPI getaddrinfo(PCSTR NodeName, PCSTR ServiceName, const ADDRINFOA* Hints, PADDRINFOA* Result);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-freeaddrinfo
 */
void WSAAPI freeaddrinfo(PADDRINFOA Result);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexw
 */
INT WSAAPI GetAddrInfoExW(PCWSTR NodeName, PCWSTR ServiceName, DWORD NameSpace, LPGUID Provider,
                          const ADDRINFOEXW* Hints, PADDRINFOEXW* Result, struct timeval* Timeout,
                          LPOVERLAPPED Overlapped, PVOID CompletionRoutine, LPHANDLE NameHandle);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-freeaddrinfoexw
 */
void WSAAPI FreeAddrInfoExW(PADDRINFOEXW Result);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_a
 */
LONG WSAAPI DnsQuery_A(PCSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results, PVOID* Reserved);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_w
 */
LONG WSAAPI DnsQuery_W(PCWSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results, PVOID* Reserved);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_utf8
 */
LONG WSAAPI DnsQuery_UTF8(PCSTR Name, WORD Type, DWORD Options, PVOID Extra, PVOID* Results, PVOID* Reserved);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsrecordlistfree
 */
void WSAAPI DnsRecordListFree(PVOID RecordList, INT FreeType);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-inetntopw
 */
PCWSTR WSAAPI InetNtopW(INT Family, const VOID* Address, PWSTR Buffer, SIZE_T BufferSize);

#ifndef _WINSOCKAPI_
struct hostent;
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-gethostbyname
 */
struct hostent* WSAAPI gethostbyname(PCSTR Name);
#endif
}

using Answer = appbox::test::ProtocolResolveName::Answer;
using Api = appbox::test::ProtocolResolveName::Api;

/**
 * @brief The header of a DNS record the DNS API returns.
 *
 * The data of a record follows the header; the probe reads the address of an A
 * and of an AAAA record from there. The ANSI, the wide and the UTF-8 entry
 * point of the DNS client share the layout, because the header holds pointers
 * of the machine word size in every one of them.
 *
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/ns-windns-dns_recorda
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

/** Return code of gethostbyname() when the name cannot be resolved. */
constexpr int kHostNotFound = 11001; /* WSAHOST_NOT_FOUND */

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
 * @brief Collect the addresses of a linked list of name resolution results.
 * @param[in] node First node of the list, may be null.
 * @param[out] answer Answer of the question.
 */
template <typename Node>
void CollectAddresses(const Node* node, Answer& answer)
{
    for (; node != nullptr && static_cast<int>(answer.addresses.size()) < kMaxRecords; node = node->ai_next)
    {
        const auto address = FormatSocketAddress(node->ai_family, node->ai_addr);
        if (!address.empty())
        {
            answer.addresses.push_back(address);
        }
    }
}

/**
 * @brief Collect the addresses of a list of DNS records.
 * @param[in] records First record of the list, may be null.
 * @param[out] answer Answer of the question.
 */
void CollectDnsRecords(PVOID records, Answer& answer)
{
    auto* node = static_cast<DnsRecordHeader*>(records);
    for (int index = 0; node != nullptr && index < kMaxRecords; ++index)
    {
        const auto* base = reinterpret_cast<const unsigned char*>(node);
        if (node->wType == DNS_TYPE_A)
        {
            answer.addresses.push_back(FormatAddress(AF_INET, base + kDnsRecordDataOffset));
        }
        else if (node->wType == DNS_TYPE_AAAA)
        {
            answer.addresses.push_back(FormatAddress(AF_INET6, base + kDnsRecordDataOffset));
        }
        node = node->pNext;
    }
}

/**
 * @brief Resolve a hostname with GetAddrInfoW.
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithGetAddrInfo(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    ADDRINFOW hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = question.family;

    PADDRINFOW result = nullptr;
    const auto text = appbox::UTF8ToWide(question.name);
    answer.code = GetAddrInfoW(text.c_str(), nullptr, &hints, &result);

    CollectAddresses(result, answer);

    if (result != nullptr)
    {
        FreeAddrInfoW(result);
    }
}

/**
 * @brief Resolve a hostname with getaddrinfo, the ANSI variant.
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithGetAddrInfoAnsi(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    ADDRINFOA hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = question.family;

    PADDRINFOA result = nullptr;
    answer.code = getaddrinfo(question.name.c_str(), nullptr, &hints, &result);

    CollectAddresses(result, answer);

    if (result != nullptr)
    {
        freeaddrinfo(result);
    }
}

/**
 * @brief Resolve a hostname with GetAddrInfoExW, the extended variant.
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithGetAddrInfoEx(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    ADDRINFOEXW hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = question.family;

    PADDRINFOEXW result = nullptr;
    const auto   text = appbox::UTF8ToWide(question.name);
    answer.code =
        GetAddrInfoExW(text.c_str(), nullptr, NS_ALL, nullptr, &hints, &result, nullptr, nullptr, nullptr, nullptr);

    CollectAddresses(result, answer);

    if (result != nullptr)
    {
        FreeAddrInfoExW(result);
    }
}

/**
 * @brief Resolve a hostname with gethostbyname, the legacy resolution.
 *
 * The legacy resolution answers IPv4 addresses only, so the probe reads them
 * from the list of the result.
 *
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithGetHostByName(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    const hostent* result = gethostbyname(question.name.c_str());
    if (result == nullptr)
    {
        answer.code = kHostNotFound;
        return;
    }

    answer.code = 0;
    for (int index = 0; result->h_addr_list != nullptr && result->h_addr_list[index] != nullptr && index < kMaxRecords;
         ++index)
    {
        const auto address = FormatAddress(result->h_addrtype, result->h_addr_list[index]);
        if (!address.empty())
        {
            answer.addresses.push_back(address);
        }
    }
}

/**
 * @brief Resolve a hostname with DnsQuery_UTF8.
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithDnsQuery(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    const auto type = static_cast<WORD>(question.type);

    PVOID records = nullptr;
    answer.code = static_cast<int>(DnsQuery_UTF8(question.name.c_str(), type, 0, nullptr, &records, nullptr));

    CollectDnsRecords(records, answer);

    if (records != nullptr)
    {
        DnsRecordListFree(records, 0);
    }
}

/**
 * @brief Resolve a hostname with DnsQuery_A, the ANSI entry point.
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithDnsQueryAnsi(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    const auto type = static_cast<WORD>(question.type);

    PVOID records = nullptr;
    answer.code = static_cast<int>(DnsQuery_A(question.name.c_str(), type, 0, nullptr, &records, nullptr));

    CollectDnsRecords(records, answer);

    if (records != nullptr)
    {
        DnsRecordListFree(records, 0);
    }
}

/**
 * @brief Resolve a hostname with DnsQuery_W, the wide entry point.
 * @param[in] question Question of the case.
 * @param[out] answer Answer of the question.
 */
void ResolveWithDnsQueryWide(const appbox::test::ProtocolResolveName::Question& question, Answer& answer)
{
    const auto type = static_cast<WORD>(question.type);
    const auto text = appbox::UTF8ToWide(question.name);

    PVOID records = nullptr;
    answer.code = static_cast<int>(DnsQuery_W(text.c_str(), type, 0, nullptr, &records, nullptr));

    CollectDnsRecords(records, answer);

    if (records != nullptr)
    {
        DnsRecordListFree(records, 0);
    }
}

/**
 * @brief Answer one question with the API it names.
 * @param[in] question Question of the case.
 * @return The answer of the question.
 */
Answer AnswerQuestion(const appbox::test::ProtocolResolveName::Question& question)
{
    Answer answer;

    switch (question.api)
    {
    case Api::GetAddrInfoAnsi:
        ResolveWithGetAddrInfoAnsi(question, answer);
        break;
    case Api::GetAddrInfoEx:
        ResolveWithGetAddrInfoEx(question, answer);
        break;
    case Api::GetHostByName:
        ResolveWithGetHostByName(question, answer);
        break;
    case Api::DnsQueryAnsi:
        ResolveWithDnsQueryAnsi(question, answer);
        break;
    case Api::DnsQueryWide:
        ResolveWithDnsQueryWide(question, answer);
        break;
    case Api::DnsQuery:
        ResolveWithDnsQuery(question, answer);
        break;
    case Api::GetAddrInfo:
    default:
        ResolveWithGetAddrInfo(question, answer);
        break;
    }

    return answer;
}

} // namespace

static nlohmann::json ProbeResolveName_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolResolveName::Req>();

    appbox::test::ProtocolResolveName::Rsp rsp;
    rsp.answers.reserve(req.questions.size());
    for (const auto& question : req.questions)
    {
        rsp.answers.push_back(AnswerQuestion(question));
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeResolveName("ResolveName", ProbeResolveName_Entry);
