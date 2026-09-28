#ifndef APPBOX_TEST_PROBE_RESOLVE_NAME_HPP
#define APPBOX_TEST_PROBE_RESOLVE_NAME_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolResolveName
{
    struct Req
    {
        /**
         * @brief Name resolution API the probe calls.
         */
        enum class Api
        {
            GetAddrInfo, /* GetAddrInfoW of ws2_32 */
            DnsQuery,    /* DnsQuery_UTF8 of dnsapi */
        };

        std::string name;       /* Hostname to resolve, encoding in UTF-8. */
        Api         api;        /* API to call. */
        int         family = 0; /* Address family of a GetAddrInfoW call. */
        int         type = 1;   /* Record type of a DnsQuery call. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, name, api, family, type)
    };

    struct Rsp
    {
        int                      code = 0;  /* Return code of the API. */
        std::vector<std::string> addresses; /* Addresses the API answered with. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, code, addresses)
    };
};

/**
 * @brief Resolve a hostname with the name resolution of the application.
 */
extern Probe ProbeResolveName;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_RESOLVE_NAME_HPP
