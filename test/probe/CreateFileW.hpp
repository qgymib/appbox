#ifndef APPBOX_TEST_PROBE_CREATEFILEW_HPP
#define APPBOX_TEST_PROBE_CREATEFILEW_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolCreateFileW
{
    static constexpr const char* Method = "CreateFileW";

    struct Req
    {
        std::string FileName;
        DWORD       dwDesiredAccess = GENERIC_READ;
        DWORD       dwShareMode = 0;
        DWORD       dwCreationDisposition = OPEN_EXISTING;
        DWORD       dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName, dwDesiredAccess, dwShareMode, dwCreationDisposition,
                                                    dwFlagsAndAttributes)
    };

    struct Rsp
    {
        DWORD code = 0; /* Error code. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code)
    };
};

/**
 * @brief CreateFileW probe.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolCreateFileW::Rsp ProbeCreateFileW_Entry(const ProtocolCreateFileW::Req& request);

} // namespace appbox::test

#endif
