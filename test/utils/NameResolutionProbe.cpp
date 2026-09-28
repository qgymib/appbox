#include "utils/NameResolutionProbe.hpp"
#include <windows.h>
#include <cstdint>
#include <string>

namespace appbox::test
{
namespace
{

/** Calling convention of the socket and resolver entry points. */
#define APPBOX_PROBE_WSAAPI __stdcall

/**
 * @brief Report whether the command line asks for the probe.
 *
 * @return Whether the option of the probe occurs in the command line.
 */
bool WantsProbe()
{
    return std::wstring(::GetCommandLineW()).find(kNameResolutionProbeOption) != std::wstring::npos;
}

/**
 * @brief Resolve a name with the socket library.
 *
 * The entry points are declared here instead of including `winsock2.h`, which
 * would have to come before the `windows.h` of the test executable. Only the
 * parameters which the probe passes are typed; the hints and the result list are
 * opaque pointers, which is what the calling convention needs.
 */
void QueryWithSockets()
{
    using GetAddrInfoWProc = int(APPBOX_PROBE_WSAAPI*)(const wchar_t*, const wchar_t*, const void*, void**);
    using FreeAddrInfoWProc = void(APPBOX_PROBE_WSAAPI*)(void*);

    /* ws2_32 is loaded on demand as well, even when the executable imports it. */
    HMODULE module = ::LoadLibraryW(L"ws2_32.dll");
    if (module == nullptr)
    {
        return;
    }

    const auto get_addr_info = reinterpret_cast<GetAddrInfoWProc>(::GetProcAddress(module, "GetAddrInfoW"));
    if (get_addr_info == nullptr)
    {
        return;
    }

    void* result = nullptr;
    if (get_addr_info(L"localhost", nullptr, nullptr, &result) != 0 || result == nullptr)
    {
        return;
    }

    const auto free_addr_info = reinterpret_cast<FreeAddrInfoWProc>(::GetProcAddress(module, "FreeAddrInfoW"));
    if (free_addr_info != nullptr)
    {
        free_addr_info(result);
    }
}

/**
 * @brief Resolve a name with the DNS client.
 *
 * The DNS client is not part of the import table of the test executable, so the
 * loader maps it when this function runs, which is what the tracer has to
 * notice to arm its breakpoints.
 */
void QueryWithDnsClient()
{
    using DnsQueryUtf8Proc =
        std::int32_t(APPBOX_PROBE_WSAAPI*)(const char*, std::uint16_t, std::uint32_t, void*, void**, void*);
    using DnsRecordListFreeProc = void(APPBOX_PROBE_WSAAPI*)(void*, std::int32_t);

    HMODULE module = ::LoadLibraryW(L"dnsapi.dll");
    if (module == nullptr)
    {
        return;
    }

    const auto query = reinterpret_cast<DnsQueryUtf8Proc>(::GetProcAddress(module, "DnsQuery_UTF8"));
    if (query == nullptr)
    {
        return;
    }

    /* DNS_TYPE_A with the standard query options. */
    void* records = nullptr;
    if (query("localhost", 1U, 0U, nullptr, &records, nullptr) != 0 || records == nullptr)
    {
        return;
    }

    const auto free_records = reinterpret_cast<DnsRecordListFreeProc>(::GetProcAddress(module, "DnsRecordListFree"));
    if (free_records != nullptr)
    {
        /* DnsFreeRecordList. */
        free_records(records, 1);
    }
}

} // namespace

bool RunNameResolutionProbeIfRequested()
{
    if (!WantsProbe())
    {
        return false;
    }

    QueryWithSockets();
    QueryWithDnsClient();
    return true;
}

} // namespace appbox::test
