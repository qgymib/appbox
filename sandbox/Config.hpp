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
    std::string                  registry_hive_dos_path; /* Registry hive file path. Encoding in UTF-8. */

    /**
     * @brief Registry isolation file paths, in the order of the layers.
     *
     * Every file carries the isolation modes of one layer of the run: the file
     * of the resources of the archive comes first and the file of every patch
     * package follows in the order the packages take effect in. A mode a later
     * file sets for a key or a value overrides the mode the files below it set
     * for the same entry, which is what makes the last layer the one the
     * sandboxed process observes.
     *
     * @note encoding in UTF-8
     */
    std::vector<std::string> registry_isolation_dos_paths;

    /**
     * @brief Filesystem isolation file paths, in the order of the layers.
     *
     * Every file carries the isolation modes of one layer of the run: the
     * file of the resources of the archive comes first and the file of every
     * patch package follows in the order the packages take effect in. A mode
     * a later file sets for a path overrides the mode the files below it set
     * for the same path, which is what makes the last layer the one the
     * sandboxed process observes.
     *
     * @note encoding in UTF-8
     */
    std::vector<std::string> filesystem_isolation_dos_paths;

    /**
     * @brief Network isolation file paths, in the order of the layers.
     *
     * Every file carries the network configuration of one layer of the run:
     * the file of the resources of the archive comes first and the file of
     * every patch package follows in the order the packages take effect in. A
     * redirection a later file lists for a hostname overrides the redirection
     * of the same hostname of the files below it, while a hostname no later
     * file lists keeps the redirection below it; the proxy of the last file
     * which names a usable one is the proxy of the run.
     *
     * @note encoding in UTF-8
     */
    std::vector<std::string> network_isolation_dos_paths;

    /**
     * @brief Environment isolation file paths, in the order of the layers.
     *
     * Every file carries the environment variables of one layer of the run:
     * the file of the resources of the archive comes first and the file of
     * every patch package follows in the order the packages take effect in.
     * Every layer applies its own isolation and merge mode to the value the
     * layers below it composed, so the composition starts at the value of the
     * host and the last layer which names a variable decides how the variable
     * of the run is built from the value below it.
     *
     * @note encoding in UTF-8
     */
    std::vector<std::string> environment_isolation_dos_paths;

    std::string environment_state_dos_path; /* Environment state file path. Encoding in UTF-8. */

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SandboxConfig, environment_is_composed, pipe_path, sandbox32_dos_path,
                                   sandbox64_dos_path, fs_upper, fs_lower, variables, registry_hive_dos_path,
                                   registry_isolation_dos_paths, filesystem_isolation_dos_paths,
                                   network_isolation_dos_paths, environment_isolation_dos_paths,
                                   environment_state_dos_path)
};

} // namespace appbox

#endif
