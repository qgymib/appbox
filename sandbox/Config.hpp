#ifndef APPBOX_SANDBOX_CONFIG_HPP
#define APPBOX_SANDBOX_CONFIG_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

struct SandboxConfig
{
    std::string pipe_path;          /* Named pipe path. Encoding in UTF-8. */
    std::string sandbox32_dos_path; /* Path to 32-bit sandbox dll path. Encoding in UTF-8. */
    std::string sandbox64_dos_path; /* Path to 64-bit sandbox dll path. Encoding in UTF-8. */
    std::string base_nt_path;       /* Base read-only filesystem path. */
    std::string overlay_nt_path;    /* Overlay filesystem path. */

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SandboxConfig, pipe_path, sandbox32_dos_path, sandbox64_dos_path, base_nt_path,
                                   overlay_nt_path)
};

} // namespace appbox

#endif
