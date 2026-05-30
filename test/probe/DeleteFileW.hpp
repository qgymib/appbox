#ifndef APPBOX_TEST_PROBE_DELETEFILEW_HPP
#define APPBOX_TEST_PROBE_DELETEFILEW_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolDeleteFileW
{
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
 */
extern Probe ProbeDeleteFileW;

} // namespace appbox::test

#endif
