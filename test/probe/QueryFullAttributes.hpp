#ifndef APPBOX_TEST_PROBE_QUERY_FULL_ATTRIBUTES_HPP
#define APPBOX_TEST_PROBE_QUERY_FULL_ATTRIBUTES_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolQueryFullAttributes
{
    struct Req
    {
        std::vector<std::string> paths; /* File names encoding in UTF-8. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, paths)
    };

    struct Item
    {
        long      status = 0;     /* NTSTATUS of the query. */
        DWORD     attributes = 0; /* File attributes, zero on failure. */
        long long size = 0;       /* End of file of the entry in bytes, zero on failure. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, status, attributes, size)
    };

    struct Rsp
    {
        std::vector<Item> items; /* One entry per path of the request, in order. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, items)
    };
};

/**
 * @brief NtQueryFullAttributesFile probe.
 *
 * The probe calls the NT entry point itself instead of the user mode wrapper,
 * so it pins the redirection of a query which carries a name directly. Every
 * path of one request is queried by the same process, which keeps the number
 * of probe calls of a case low.
 */
extern Probe ProbeQueryFullAttributes;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_QUERY_FULL_ATTRIBUTES_HPP
