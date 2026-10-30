#ifndef APPBOX_TEST_PROBE_CREATEDIRECTORYW_HPP
#define APPBOX_TEST_PROBE_CREATEDIRECTORYW_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolCreateDirectoryW
{
    static constexpr const char* Method = "CreateDirectoryW";

    struct Req
    {
        std::string PathName;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, PathName)
    };

    struct Rsp
    {
        DWORD code = 0; /* Error code. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code)
    };
};

/**
 * @brief CreateDirectoryW probe.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolCreateDirectoryW::Rsp ProbeCreateDirectoryW_Entry(const ProtocolCreateDirectoryW::Req& request);

} // namespace appbox::test

#endif
