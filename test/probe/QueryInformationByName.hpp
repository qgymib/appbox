#ifndef APPBOX_TEST_PROBE_QUERY_INFORMATION_BY_NAME_HPP
#define APPBOX_TEST_PROBE_QUERY_INFORMATION_BY_NAME_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolQueryInformationByName
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
 * @brief NtQueryInformationByName probe.
 *
 * The probe calls the NT entry point with `FileStatInformation`, which is the
 * information class the entry point is meant for: it reports the attributes of
 * an entry without opening a handle, so it pins the redirection of a name based
 * query directly. Every path of one request is queried by the same process,
 * which keeps the number of probe calls of a case low.
 */
extern Probe ProbeQueryInformationByName;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_QUERY_INFORMATION_BY_NAME_HPP
