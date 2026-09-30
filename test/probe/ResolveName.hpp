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
    /**
     * @brief Name resolution API the probe calls.
     *
     * Every entry point the sandbox hooks has a value of its own, so a case can
     * pin the redirection of each of them.
     */
    enum class Api
    {
        GetAddrInfo,     /* GetAddrInfoW of ws2_32 */
        GetAddrInfoAnsi, /* getaddrinfo of ws2_32 */
        GetAddrInfoEx,   /* GetAddrInfoExW of ws2_32 */
        GetHostByName,   /* gethostbyname of ws2_32 */
        DnsQuery,        /* DnsQuery_UTF8 of dnsapi */
        DnsQueryAnsi,    /* DnsQuery_A of dnsapi */
        DnsQueryWide,    /* DnsQuery_W of dnsapi */
    };

    /**
     * @brief One question of a resolution.
     */
    struct Question
    {
        std::string name;       /* Hostname to resolve, encoding in UTF-8. */
        Api         api;        /* API to call. */
        int         family = 0; /* Address family of a winsock call. */
        int         type = 1;   /* Record type of a DNS client call. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Question, name, api, family, type)
    };

    /**
     * @brief The answer of one question.
     */
    struct Answer
    {
        int                      code = 0;  /* Return code of the API, zero on success. */
        std::vector<std::string> addresses; /* Addresses the API answered with. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Answer, code, addresses)
    };

    /**
     * @brief The questions of one probe call.
     *
     * A call answers every question of its list inside the probe process the
     * case started, so the cost of the chain - one launcher start and one
     * injection of the sandbox - is paid once per case instead of once per
     * question.
     */
    struct Req
    {
        std::vector<Question> questions;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, questions)
    };

    struct Rsp
    {
        std::vector<Answer> answers; /* One answer per question, in the order of the questions. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, answers)
    };
};

/**
 * @brief Resolve hostnames with the name resolution of the application.
 */
extern Probe ProbeResolveName;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_RESOLVE_NAME_HPP
