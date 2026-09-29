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

struct SandboxVariable
{
    /**
     * @brief Name of the variable without the `%APPBOX:` prefix.
     *
     * For example `"ProgramFiles"`. The sandbox compares the name of a
     * reference of a value with this name ignoring the case.
     *
     * @note encoding in UTF-8
     */
    std::string name;

    /**
     * @brief Path the reference of a value is replaced with.
     *
     * The real path of the known folder on the machine which runs the sandbox,
     * for example `C:\Program Files`.
     *
     * @note encoding in UTF-8
     */
    std::string path;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SandboxVariable, name, path)
};

struct SandboxConfig
{
    /**
     * @brief Whether the environment of the process is composed already.
     *
     * A process the loader starts directly composes its environment from the
     * environment of the host, the isolation file of the archive and the state
     * of an earlier run. A process a sandboxed application starts inherits the
     * environment of its parent, which is composed already: the flag tells the
     * sandbox to keep the inherited environment as it is and to continue the
     * state of the sandbox instead of composing it a second time.
     */
    bool environment_is_composed = false;

    std::string                  pipe_path;          /* Named pipe path. Encoding in UTF-8. */
    std::string                  sandbox32_dos_path; /* Path to 32-bit sandbox dll path. Encoding in UTF-8. */
    std::string                  sandbox64_dos_path; /* Path to 64-bit sandbox dll path. Encoding in UTF-8. */
    std::string                  fs_upper;  /* Overlay filesystem path, no trailing slash. Encoding in UTF-8. */
    std::vector<SandboxLowerFS>  fs_lower;  /* Sandbox read-only filesystem layers. */
    std::vector<SandboxVariable> variables; /* Variables the sandbox expands in the values of the workspace. */
    std::string                  registry_hive_dos_path;        /* Registry hive file path. Encoding in UTF-8. */
    std::string                  registry_isolation_dos_path;   /* Registry isolation file path. Encoding in UTF-8. */
    std::string                  filesystem_isolation_dos_path; /* Filesystem isolation file path. Encoding in UTF-8. */
    std::string                  network_isolation_dos_path;    /* Network isolation file path. Encoding in UTF-8. */
    std::string environment_isolation_dos_path; /* Environment isolation file path. Encoding in UTF-8. */
    std::string environment_state_dos_path;     /* Environment state file path. Encoding in UTF-8. */

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SandboxConfig, environment_is_composed, pipe_path, sandbox32_dos_path,
                                   sandbox64_dos_path, fs_upper, fs_lower, variables, registry_hive_dos_path,
                                   registry_isolation_dos_path, filesystem_isolation_dos_path,
                                   network_isolation_dos_path, environment_isolation_dos_path,
                                   environment_state_dos_path)
};

} // namespace appbox

#endif
