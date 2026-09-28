#ifndef APPBOX_TEST_PROBE_REMOVEDIRECTORY_HPP
#define APPBOX_TEST_PROBE_REMOVEDIRECTORY_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolRemoveDirectory
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
 * @brief RemoveDirectoryW probe.
 */
extern Probe ProbeRemoveDirectory;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REMOVEDIRECTORY_HPP
