#ifndef APPBOX_LAUNCHER_UTILS_SANDBOX_PATHS_HPP
#define APPBOX_LAUNCHER_UTILS_SANDBOX_PATHS_HPP

#include <cstddef>
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
 * the state of the sandbox, which the launcher creates at run time. Both are
 * resolved against the directory of the launcher configuration, which is the
 * directory of the launcher program itself unless the configuration was given
 * with `--X-AppBox-ConfigFile`.
 *
 * The patch packages of the user are resolved the same way: the user creates
 * `patch` next to the launcher, and the launcher extracts a package into
 * `data/cache`, which is the cache directory of the state directory it creates
 * as soon as that directory holds a package. The same cache directory carries
 * the two sandbox injection modules the launcher releases from its own
 * resources at every start.
 */
struct SandboxPaths
{
    /**
     * @brief Resolve the layout against the directory of the configuration.
     * @param[in] config_dir Directory which holds the launcher configuration.
     * @return The resolved paths.
     */
    static SandboxPaths Resolve(const std::wstring& config_dir)
    {
        const std::filesystem::path root(config_dir);

        SandboxPaths paths;
        paths.root = config_dir;
        paths.app = (root / layout::kAppDirNameW).wstring();
        paths.state = (root / layout::kStateDirNameW).wstring();
        paths.patch = (root / layout::kPatchDirNameW).wstring();
        /* The constant is relative to the directory of the launcher. */
        paths.cache = (root / layout::kStateCacheRelative).wstring();
        return paths;
    }

    /**
     * @brief Name of the cache entry of a patch package.
     *
     * The entry is named after the package file without the extension of a
     * package, so `00-foo.zip` is extracted into `cache/00-foo`. The name
     * carries no path separator: it is a file name of the patch directory.
     *
     * @param[in] package_name File name of a patch package.
     * @return The name of the cache entry, empty when the name carries no
     *         package extension.
     */
    static std::wstring PatchCacheName(const std::wstring& package_name)
    {
        const std::wstring extension = layout::kPatchPackageExtensionW;
        if (package_name.size() <= extension.size())
        {
            return {};
        }

        const auto offset = package_name.size() - extension.size();
        for (std::size_t index = 0; index < extension.size(); ++index)
        {
            auto left = package_name[offset + index];
            auto right = extension[index];
            if (left >= L'A' && left <= L'Z')
            {
                left = static_cast<wchar_t>(left - L'A' + L'a');
            }
            if (right >= L'A' && right <= L'Z')
            {
                right = static_cast<wchar_t>(right - L'A' + L'a');
            }
            if (left != right)
            {
                return {};
            }
        }

        return package_name.substr(0, offset);
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
     * changes its environment while it runs and the launcher writes the document
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
     * @brief Hive which carries the state of the sandbox.
     *
     * The file lives in the state directory, because mounting a hive writes to
     * it; the sandbox creates it when it mounts it, so it holds the
     * modifications of the runs and nothing else.
     *
     * @return The path of the hive inside the state directory.
     */
    std::wstring StateRegistryHiveFile() const
    {
        return (std::filesystem::path(state) / layout::kRegistryDirNameW / layout::kRegistryHiveFileNameW).wstring();
    }

    /**
     * @brief Directory which carries the read-only image of the registry.
     *
     * The image is the content of the registry of `app` merged with the hives
     * of the patch packages, and it is rebuilt at every start. The directory
     * holds the hive and the log files the mount keeps next to it, so the whole
     * directory is replaced when the image is rebuilt.
     *
     * @return The path of the directory inside the state directory.
     */
    std::wstring StateRegistryImageDir() const
    {
        return (std::filesystem::path(StateRegistryImageHiveFile()).parent_path()).wstring();
    }

    /**
     * @brief Read-only image of the registry of the run.
     *
     * @return The path of the image hive inside the state directory.
     */
    std::wstring StateRegistryImageHiveFile() const
    {
        /* The constant is relative to the directory of the launcher. */
        return (std::filesystem::path(root) / layout::kStateRegistryImageRelative).wstring();
    }

    /**
     * @brief Directory which carries the extracted patch packages.
     *
     * Every package has a directory of its own below it, named after the
     * package file without its extension. The directory is created by the
     * launcher as soon as the patch directory holds at least one package.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the directory of one package.
     */
    std::wstring PatchCacheDir(const std::wstring& name) const
    {
        return (std::filesystem::path(cache) / name).wstring();
    }

    /**
     * @brief Root of the filesystem layers of one extracted package.
     *
     * A patch package roots the resource tree at the archive root, so the
     * layers of a package live in the `filesystem` directory of its cache
     * entry, exactly like the layers of `app` live in the `filesystem`
     * directory of the resource root.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the layer root of the package.
     */
    std::wstring PatchLayerRoot(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchCacheDir(name)) / layout::kFilesystemDirNameW).wstring();
    }

    /**
     * @brief Digest file of one cache entry.
     *
     * The file records the digest of the package the entry was extracted
     * from; it is the last file the launcher writes into the entry, so its
     * content decides whether the extraction describes the current package.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the digest file of the package.
     */
    std::wstring PatchDigestFile(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchCacheDir(name)) / layout::kPatchDigestFileNameW).wstring();
    }

    /**
     * @brief Isolation modes of the filesystem workspace of one package.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the isolation file inside the cache entry; the file
     *         does not have to exist, because a package may override the
     *         content of the filesystem without setting a mode.
     */
    std::wstring PatchIsolationFile(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchLayerRoot(name)) / layout::kIsolationFileNameW).wstring();
    }

    /**
     * @brief Virtual registry of one package.
     *
     * A package roots the resource tree at the archive root, so the hive of a
     * package lives in the `registry` directory of its cache entry, exactly
     * like the hive of `app` lives in the `registry` directory of the resource
     * root.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the hive inside the cache entry; the file does not
     *         have to exist, because a package may override the filesystem
     *         without carrying a registry.
     */
    std::wstring PatchRegistryHive(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchCacheDir(name)) / layout::kRegistryDirNameW / layout::kRegistryHiveFileNameW)
            .wstring();
    }

    /**
     * @brief Isolation modes of the registry workspace of one package.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the isolation file inside the cache entry; the file
     *         does not have to exist, because a package may override the
     *         registry without setting a mode.
     */
    std::wstring PatchRegistryIsolationFile(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchCacheDir(name)) / layout::kRegistryDirNameW / layout::kIsolationFileNameW)
            .wstring();
    }

    /**
     * @brief Network configuration of one package.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the isolation file inside the cache entry; the file
     *         does not have to exist, because a package may override other
     *         resources without carrying a network configuration.
     */
    std::wstring PatchNetworkIsolationFile(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchCacheDir(name)) / layout::kNetworkDirNameW / layout::kIsolationFileNameW)
            .wstring();
    }

    /**
     * @brief Environment variables of one package.
     *
     * @param[in] name Name of the cache entry, see PatchCacheName().
     * @return The path of the isolation file inside the cache entry; the file
     *         does not have to exist, because a package may override other
     *         resources without carrying environment variables.
     */
    std::wstring PatchEnvironmentIsolationFile(const std::wstring& name) const
    {
        return (std::filesystem::path(PatchCacheDir(name)) / layout::kEnvironmentDirNameW / layout::kIsolationFileNameW)
            .wstring();
    }

    /**
     * @brief Directory the layout was resolved against.
     *
     * It is the directory of the configuration of the run, which is the
     * directory of the launcher program unless the configuration was given with
     * `--X-AppBox-ConfigFile`. The directory carries the configuration itself
     * and the state of the sandbox; the log files of the run are written into
     * the log directory of the run, which is a directory of its own (see
     * `--X-AppBox-LogDir` of the launcher).
     */
    std::wstring root;

    std::wstring app;   /* Absolute path of the read-only resource root. */
    std::wstring state; /* Absolute path of the writable state root. */
    std::wstring patch; /* Absolute path of the directory of the patch packages. */
    std::wstring cache; /* Absolute path of the cache inside the state root: released modules and extracted packages. */

    /**
     * @brief Replace the read-only resource root of the run.
     *
     * A self-extracting program keeps its resources in the temporary folder it
     * extracts the payload into, while the state, the patch packages and the
     * log directory stay beside the program itself.
     *
     * @param[in] resource_root Absolute path of the resource root (`...\app`).
     */
    void SetResourceRoot(const std::wstring& resource_root)
    {
        app = resource_root;
    }
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_SANDBOX_PATHS_HPP
