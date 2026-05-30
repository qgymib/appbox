#ifndef APPBOX_TEST_PROBE_READFILE_FULL_HPP
#define APPBOX_TEST_PROBE_READFILE_FULL_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolReadFileFull
{

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
 */
extern Probe ProbeReadFileFull;

} // namespace appbox::test

#endif
