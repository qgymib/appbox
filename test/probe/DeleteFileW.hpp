#ifndef APPBOX_TEST_PROBE_DELETEFILEW_HPP
#define APPBOX_TEST_PROBE_DELETEFILEW_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolDeleteFileW
{
    static constexpr const char* Method = "DeleteFileW";

    struct Req
    {
        std::string FileName;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName)
    };

    struct Rsp
    {
        DWORD code = 0; /* Error code. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code)
    };
};

/**
 * @brief DeleteFileW probe.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolDeleteFileW::Rsp ProbeDeleteFileW_Entry(const ProtocolDeleteFileW::Req& request);

} // namespace appbox::test

#endif
