#ifndef APPBOX_TEST_PROBE_QUERY_ATTRIBUTES_HPP
#define APPBOX_TEST_PROBE_QUERY_ATTRIBUTES_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolQueryAttributes
{
    static constexpr const char* Method = "QueryAttributes";

    struct Req
    {
        std::string FileName; /* File name encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName)
    };

    struct Rsp
    {
        DWORD code = 0;       /* Error code, zero on success. */
        DWORD attributes = 0; /* File attributes, INVALID_FILE_ATTRIBUTES on failure. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, attributes)
    };
};

/**
 * @brief GetFileAttributes probe (NtQueryAttributesFile based).
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolQueryAttributes::Rsp ProbeQueryAttributes_Entry(const ProtocolQueryAttributes::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_QUERY_ATTRIBUTES_HPP
