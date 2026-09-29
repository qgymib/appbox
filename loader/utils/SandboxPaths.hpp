#ifndef APPBOX_LOADER_UTILS_SANDBOX_PATHS_HPP
#define APPBOX_LOADER_UTILS_SANDBOX_PATHS_HPP

#include <filesystem>
#include <string>
#include "SandboxLayout.hpp"

namespace appbox
{

/**
 * @brief Absolute paths of the sandbox layout of one run.
 *
 * The layout is the fixed convention of `common/SandboxLayout.hpp`: `app`
 * carries the read-only resources of the packed application and `data` carries
 * the state of the sandbox, which the loader creates at run time. Both are
 * resolved against the directory of the loader configuration, which is the
 * directory of the loader program itself unless the configuration was given
 * with `--X-AppBox-ConfigFile`.
 */
struct SandboxPaths
{
    /**
     * @brief Resolve the layout against the directory of the configuration.
     * @param[in] config_dir Directory which holds the loader configuration.
     * @return The resolved paths.
     */
    static SandboxPaths Resolve(const std::wstring& config_dir)
    {
        const std::filesystem::path root(config_dir);

        SandboxPaths paths;
        paths.app = (root / layout::kAppDirNameW).wstring();
        paths.state = (root / layout::kStateDirNameW).wstring();
        return paths;
    }

    /**
     * @brief Root of the read-only layers of the filesystem.
     * @return The path of the folder which holds one directory per layer key.
     */
    std::wstring LayerRoot() const
    {
        return (std::filesystem::path(app) / layout::kFilesystemDirNameW).wstring();
    }

    /**
     * @brief Isolation modes of the filesystem workspace.
     * @return The path of the isolation file inside the filesystem domain.
     */
    std::wstring FilesystemIsolationFile() const
    {
        return (std::filesystem::path(app) / layout::kFilesystemDirNameW / layout::kIsolationFileNameW).wstring();
    }

    /**
     * @brief Virtual registry of the workspace, as packed by the packer.
     * @return The path of the hive file inside the registry domain.
     */
    std::wstring RegistryHiveFile() const
    {
        return (std::filesystem::path(app) / layout::kRegistryDirNameW / layout::kRegistryHiveFileNameW).wstring();
    }

    /**
     * @brief Isolation modes of the registry workspace.
     * @return The path of the isolation file inside the registry domain.
     */
    std::wstring RegistryIsolationFile() const
    {
        return (std::filesystem::path(app) / layout::kRegistryDirNameW / layout::kIsolationFileNameW).wstring();
    }

    /**
     * @brief Network configuration of the workspace.
     * @return The path of the isolation file inside the network domain.
     */
    std::wstring NetworkIsolationFile() const
    {
        return (std::filesystem::path(app) / layout::kNetworkDirNameW / layout::kIsolationFileNameW).wstring();
    }

    /**
     * @brief Environment variables of the environment workspace.
     * @return The path of the isolation file inside the environment domain.
     */
    std::wstring EnvironmentIsolationFile() const
    {
        return (std::filesystem::path(app) / layout::kEnvironmentDirNameW / layout::kIsolationFileNameW).wstring();
    }

    /**
     * @brief Environment variables the packaged application changed.
     *
     * The file lives in the state directory, because the sandboxed process
     * changes its environment while it runs and the loader writes the document
     * the sandbox sends over the RPC pipe. A missing file means that the
     * application never changed its environment, so deleting the state
     * directory resets the sandbox to the environment of the archive.
     *
     * @return The path of the state file inside the state directory.
     */
    std::wstring StateEnvironmentFile() const
    {
        return (std::filesystem::path(state) / layout::kEnvironmentDirNameW / layout::kEnvironmentStateFileNameW)
            .wstring();
    }

    /**
     * @brief Hive the sandbox mounts.
     *
     * The file lives in the state directory, because mounting a hive writes to
     * it; the loader seeds it from RegistryHiveFile() on the first run.
     *
     * @return The path of the hive inside the state directory.
     */
    std::wstring StateRegistryHiveFile() const
    {
        return (std::filesystem::path(state) / layout::kRegistryDirNameW / layout::kRegistryHiveFileNameW).wstring();
    }

    std::wstring app;   /* Absolute path of the read-only resource root. */
    std::wstring state; /* Absolute path of the writable state root. */
};

} // namespace appbox

#endif // APPBOX_LOADER_UTILS_SANDBOX_PATHS_HPP
