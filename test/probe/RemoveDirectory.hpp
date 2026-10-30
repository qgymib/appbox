#ifndef APPBOX_TEST_PROBE_REMOVEDIRECTORY_HPP
#define APPBOX_TEST_PROBE_REMOVEDIRECTORY_HPP

#include "WinAPI.h" /* Must be first include file */
#include <string>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolRemoveDirectory
{
    static constexpr const char* Method = "RemoveDirectory";

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
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolRemoveDirectory::Rsp ProbeRemoveDirectory_Entry(const ProtocolRemoveDirectory::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REMOVEDIRECTORY_HPP
