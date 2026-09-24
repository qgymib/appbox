#ifndef APPBOX_SANDBOX_CONFIG_HPP
#define APPBOX_SANDBOX_CONFIG_HPP

#include <nlohmann/json.hpp>
#include <vector>

namespace appbox
{

struct SandboxLowerFS
{
    /**
     * @brief Mapped NT path in sandbox.
     *
     * For example, `\??\C:\Users\foo\AppData\Roaming`
     *
     * @note no trailing slash
     * @note encoding in UTF-8
     */
    std::string mapped_nt_path;

    /**
     * @brief Host NT path.
     *
     * For example, `\??\D:\Sandbox\AppData\Roaming`
     *
     * @note no trailing slash
     * @note encoding in UTF-8
     */
    std::string host_nt_path;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SandboxLowerFS, mapped_nt_path, host_nt_path)
};

struct SandboxConfig
{
    std::string                 pipe_path;          /* Named pipe path. Encoding in UTF-8. */
    std::string                 sandbox32_dos_path; /* Path to 32-bit sandbox dll path. Encoding in UTF-8. */
    std::string                 sandbox64_dos_path; /* Path to 64-bit sandbox dll path. Encoding in UTF-8. */
    std::string                 fs_upper;           /* Overlay filesystem path, no trailing slash. Encoding in UTF-8. */
    std::vector<SandboxLowerFS> fs_lower;           /* Sandbox read-only filesystem layers. */
    std::string                 registry_hive_dos_path; /* Registry hive file path. Encoding in UTF-8. */
    std::string                 registry_isolation_dos_path; /* Registry isolation file path. Encoding in UTF-8. */
    std::string filesystem_isolation_dos_path; /* Filesystem isolation file path. Encoding in UTF-8. */

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SandboxConfig, pipe_path, sandbox32_dos_path, sandbox64_dos_path, fs_upper,
                                   fs_lower, registry_hive_dos_path, registry_isolation_dos_path,
                                   filesystem_isolation_dos_path)
};

} // namespace appbox

#endif
