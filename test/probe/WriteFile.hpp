#ifndef APPBOX_TEST_PROBE_WRITEFILE_HPP
#define APPBOX_TEST_PROBE_WRITEFILE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolWriteFile
{
    struct Req
    {
        std::string FileName;
        std::string Data; /* Content to write, the file is truncated to it. */
        DWORD       dwCreationDisposition = OPEN_EXISTING;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName, Data, dwCreationDisposition)
    };

    struct Rsp
    {
        DWORD       code = 0; /* Error code, zero on success. */
        std::string readback; /* Content which was read back after the write. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, readback)
    };
};

/**
 * @brief WriteFile probe.
 *
 * The probe opens the file for reading and writing, writes the content at the
 * beginning of the file, truncates the file to the content and reads it back,
 * so a case pins what the view holds after the write.
 */
extern Probe ProbeWriteFile;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_WRITEFILE_HPP
