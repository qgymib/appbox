#ifndef APPBOX_PACKER_CORE_PACK_SERVICE_HPP
#define APPBOX_PACKER_CORE_PACK_SERVICE_HPP

#include "BuildReport.hpp"
#include "EnvironmentModel.hpp"
#include "FilesystemIsolationModel.hpp"
#include "NetworkModel.hpp"
#include "PackModel.hpp"
#include "RegistryModel.hpp"
#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Number of archive entries which do not come from an import.
 *
 * The loader payload, the loader configuration, the two registry artifacts
 * (the hive and the isolation file), the isolation file of the filesystem
 * workspace, the isolation file of the network workspace and the isolation
 * file of the environment workspace. The pack run and the extraction of a
 * `Build and Run` run report the same total, so both count this constant
 * instead of a literal.
 */
inline constexpr std::size_t kNonContentArchiveEntries = 7;

/**
 * @brief Number of patch archive entries which do not come from an import.
 *
 * A patch package carries the resources of a standalone archive without the
 * loader: the loader payload and the loader configuration do not travel, so
 * the count is two below kNonContentArchiveEntries. What remains is the two
 * registry artifacts (the hive and the isolation file) and the isolation file
 * of the filesystem, of the network and of the environment workspace. The
 * patch run and the extraction of a `Build and Run` run of a standalone
 * archive report the same total for their own kind, so both count a constant
 * instead of a literal.
 */
inline constexpr std::size_t kNonContentPatchEntries = 5;

/**
 * @brief Count the regular files below a folder.
 *
 * Symlinks and reparse points are not followed; their targets are not
 * counted separately.
 *
 * @param[in] folder Folder to scan.
 * @return The number of regular files in the whole subtree.
 */
std::size_t CountFilesBelow(const std::wstring& folder);

/**
 * @brief Count the files a pack run writes from the imports of the model.
 *
 * The count covers the regular files of every imported folder plus the
 * individually imported files. A pack run adds the non content entries of its
 * product on top of it, so both the run and the caller which prepares the
 * progress dialog derive the same total from the same number.
 *
 * @param[in] model The pack model.
 * @return The number of files which come from an import.
 */
std::size_t ContentFileCount(const PackModel& model);

/**
 * @brief Get the archive entry name of the loader program.
 *
 * The loader is named after the first startup file of the model, so the
 * extracted archive looks like the packaged application: an entry program
 * named `foo.exe` (optionally inside a subdirectory of the imported folder)
 * produces the loader entry `foo.exe`. The loader resolves its configuration
 * as `<own file name>.json` beside itself, so the archive entry of the
 * configuration is `<loader entry>.json`, e.g. `foo.exe.json`.
 *
 * @param[in] model The pack model.
 * @return The file name of the loader entry, empty when the model has no
 *         startup file.
 */
std::wstring LoaderEntryName(const PackModel& model);

/**
 * @brief Pack the model into a self-contained zip archive.
 *
 * The archive layout is the fixed convention of `common/SandboxLayout.hpp`:
 * the read-only resources of the packaged application travel below `app`, one
 * directory per isolation domain, while the `data` directory of the sandbox
 * does not travel at all. The loader creates it at run time, next to `app`, so
 * deleting it resets the sandbox to the state the archive carries.
 *
 * ```
 * <first startup file name>              loader payload (loader_bytes)
 * <first startup file name>.json         startups[] = { trigger, auto_start,
 *                                        executable = <layer key>\<import>\<exe> }
 * app/filesystem/isolation.json          isolation modes of the filesystem
 * app/filesystem/<layer key>/<import>/... imported folder content
 * app/filesystem/<layer key>/<target>/<file> imported file content
 * app/registry/user.hiv                  virtual registry of the workspace
 * app/registry/isolation.json            isolation modes of the registry
 * app/network/isolation.json             network configuration of the workspace
 * app/environment/isolation.json         environment variables of the workspace
 * ```
 *
 * The loader program and its configuration carry the file name of the first
 * startup file, see LoaderEntryName(). The entry programs themselves keep
 * their place below the layer tree.
 *
 * The registry artifacts land in the registry domain of the resources: the
 * hive holds the virtual registry the packaged application sees and the
 * isolation file holds the modes which decide which host entries stay visible
 * (see `common/RegistryIsolation.hpp`). The loader seeds the hive into its
 * state directory before the sandbox mounts it, because mounting a hive writes
 * to the file and the resources below `app` stay read-only.
 *
 * The loader program also carries the file icon of the first startup file:
 * the icon group which the shell shows for that program is appended to the
 * loader payload (see ApplyApplicationIcon()), so Explorer shows the icon of
 * the packaged application for the extracted program while the loader keeps
 * its own icon resources. A startup file without an icon leaves the payload
 * unchanged; the run then only logs a warning instead of failing.
 *
 * The loader bytes are supplied by the caller so unit tests can inject a
 * fake payload without a real loader binary.
 *
 * The progress total covers the files of the imported folders plus the
 * individually imported files and kNonContentArchiveEntries, so the callback
 * receives a stable upper bound for the whole run. The loader payload and its
 * configuration are reported as the preparing stage before the first imported
 * file is packed.
 *
 * Every report names the file which is being packed through
 * BuildProgress::current, using the path below the import root prefixed by the
 * import name, e.g. `L"MyApp\bin\tool.exe"`.
 *
 * The filesystem isolation modes land in the filesystem domain as
 * `app/filesystem/isolation.json`, next to the layers they describe: the
 * loader hands the file to the sandbox, which redirects the filesystem of the
 * packaged application through the modes (see
 * `common/FilesystemIsolation.hpp`). The loader skips the file while it
 * enumerates the layers of that folder.
 *
 * The network configuration of the workspace lands in the network domain as
 * `app/network/isolation.json`: the loader hands the file to the sandbox,
 * which answers a name resolution of the packaged application from its DNS
 * redirections and sends its traffic through its proxy (see
 * `common/NetworkIsolation.hpp`).
 *
 * The environment variables of the workspace land in the environment domain as
 * `app/environment/isolation.json`: the loader hands the file to the sandbox,
 * which composes the environment of the packaged application from its entries
 * and keeps the modifications the application makes to itself inside the
 * sandbox (see `common/EnvironmentIsolation.hpp`).
 *
 * @param[in] model The pack model.
 * @param[in] registry Virtual registry of the workspace, which is written into
 *                     the registry domain of the archive as a hive file and an
 *                     isolation file.
 * @param[in] isolation Isolation modes of the virtual filesystem, which are
 *                      written into the filesystem domain of the archive as an
 *                      isolation file.
 * @param[in] network DNS redirections of the network workspace, which are
 *                    written into the network domain of the archive as an
 *                    isolation file.
 * @param[in] environment Environment variables of the workspace, which are
 *                        written into the environment domain of the archive as
 *                        an isolation file.
 * @param[in] loader_bytes Embedded AppBoxLoader.exe payload.
 * @param[in] loader_size Payload size in bytes.
 * @param[in] zip_path Destination zip path (truncated when it exists).
 * @param[in] progress Called once per packed file; returning false aborts the
 *                     pack with kBuildCancelledError. May be empty to disable
 *                     progress reporting.
 * @return Error description, empty on success.
 */
std::string Pack(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                 const NetworkModel& network, const EnvironmentModel& environment, const void* loader_bytes,
                 std::size_t loader_size, const std::wstring& zip_path, const BuildProgressCallback& progress);

/**
 * @brief Pack the resources of the model into a patch package.
 *
 * The archive holds the very same resource tree a standalone archive keeps
 * below `app`, rooted at the archive root instead: the loader program, its
 * configuration and the `app` directory itself do not travel. The package is
 * meant to be dropped into the `patch` directory next to the loader of a
 * standalone archive, which merges every patch of that directory in ascending
 * name order on top of the resources of `app`, so a later package overrides an
 * earlier one and both of them override `app`.
 *
 * ```
 * filesystem/isolation.json            isolation modes of the filesystem
 * filesystem/<layer key>/<import>/...  imported folder content
 * filesystem/<layer key>/<target>/...  imported file content
 * registry/user.hiv                    virtual registry of the workspace
 * registry/isolation.json              isolation modes of the registry
 * network/isolation.json               network configuration of the workspace
 * environment/isolation.json           environment variables of the workspace
 * ```
 *
 * The startup files of the model are neither required nor used: a patch
 * package carries no loader which could start them, so the model of a patch
 * project may be empty of startup files.
 *
 * The progress total covers the files of the imported folders plus the
 * individually imported files and kNonContentPatchEntries, so the callback
 * receives a stable upper bound for the whole run. The run opens with the
 * preparing stage like a standalone pack run does.
 *
 * @param[in] model The pack model.
 * @param[in] registry Virtual registry of the workspace, which is written into
 *                     the registry domain of the package as a hive file and an
 *                     isolation file.
 * @param[in] isolation Isolation modes of the virtual filesystem, which are
 *                      written into the filesystem domain of the package.
 * @param[in] network DNS redirections of the network workspace, which are
 *                    written into the network domain of the package.
 * @param[in] environment Environment variables of the workspace, which are
 *                        written into the environment domain of the package.
 * @param[in] zip_path Destination zip path (truncated when it exists).
 * @param[in] progress Called once per packed file; returning false aborts the
 *                     pack with kBuildCancelledError. May be empty to disable
 *                     progress reporting.
 * @return Error description, empty on success.
 */
std::string PackPatch(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                      const NetworkModel& network, const EnvironmentModel& environment, const std::wstring& zip_path,
                      const BuildProgressCallback& progress);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PACK_SERVICE_HPP
