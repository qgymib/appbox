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
        std::string path;                /* Directory path. */
        bool        extended = false;    /* Use NtQueryDirectoryFileEx. */
        std::string info_class;          /* Information class, empty for FileFullDirectoryInformation. */
        bool        create_file = false; /* Open the directory with CreateFileW. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, path, extended, info_class, create_file)
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
 *
 * A request may name the information class of the query (empty asks for
 * `FileFullDirectoryInformation`) and may ask for a handle of `CreateFileW`
 * instead of one of `NtOpenFile`, which is the second way the sandbox learns a
 * directory handle. The probe reads the name of an entry with its own table of
 * the classes, so a case which compares the answer with the names it created
 * itself pins the merge and the layout of the class at once.
 */
extern Probe ProbeListDirNt;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_LIST_DIR_NT_HPP
