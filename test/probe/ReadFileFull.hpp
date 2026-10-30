#ifndef APPBOX_TEST_PROBE_READFILE_FULL_HPP
#define APPBOX_TEST_PROBE_READFILE_FULL_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolReadFileFull
{
    static constexpr const char* Method = "ReadFileFull";

    struct Req
    {
        std::string FileName; /* File name encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName)
    };

    struct Rsp
    {
        DWORD       code = 0; /* Error code. */
        std::string data;     /* Binary data. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, data)
    };
};

/**
 * @brief ReadFile probe.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolReadFileFull::Rsp ProbeReadFileFull_Entry(const ProtocolReadFileFull::Req& request);

} // namespace appbox::test

#endif
