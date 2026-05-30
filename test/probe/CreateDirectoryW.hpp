#ifndef APPBOX_TEST_PROBE_CREATEDIRECTORYW_HPP
#define APPBOX_TEST_PROBE_CREATEDIRECTORYW_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolCreateDirectoryW
{
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
 */
extern Probe ProbeCreateDirectoryW;

} // namespace appbox::test

#endif
