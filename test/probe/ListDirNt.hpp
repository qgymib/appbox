#ifndef APPBOX_TEST_PROBE_LIST_DIR_NT_HPP
#define APPBOX_TEST_PROBE_LIST_DIR_NT_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolListDirNt
{
    struct Req
    {
        std::string path;              /* Directory path. */
        bool        extended = false;  /* Use NtQueryDirectoryFileEx. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, path, extended)
    };

    struct Rsp
    {
        long                     status = 0; /* NTSTATUS of the enumeration. */
        std::vector<std::string> names;      /* Names the query reported. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, status, names)
    };
};

/**
 * @brief Enumerate a directory through the NT entry points.
 *
 * The probe opens the directory with `NtOpenFile`, which registers the handle
 * with the sandbox, and enumerates it with `NtQueryDirectoryFile` or
 * `NtQueryDirectoryFileEx`, one entry per call. It therefore pins the merged
 * view of both entry points, while the other probes exercise the user mode
 * wrappers which may use either of them.
 */
extern Probe ProbeListDirNt;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_LIST_DIR_NT_HPP
