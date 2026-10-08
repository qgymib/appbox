#ifndef APPBOX_PACKER_CORE_PACK_SERVICE_HPP
#define APPBOX_PACKER_CORE_PACK_SERVICE_HPP

#include "ApplicationMetadata.hpp"
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
 * The launcher payload, the launcher configuration, the two sandbox injection
 * modules below `app`, the two registry artifacts (the hive and the isolation
 * file), the isolation file of the filesystem workspace, the isolation file of
 * the network workspace and the isolation file of the environment workspace.
 * The pack run and the extraction of a `Build and Run` run report the same
 * total, so both count this constant instead of a literal.
 */
inline constexpr std::size_t kNonContentArchiveEntries = 9;

/**
 * @brief Number of patch archive entries which do not come from an import.
 *
 * A patch package carries the resources of a standalone archive without the
 * program which starts it: the launcher payload, the launcher configuration and
 * the two sandbox injection modules do not travel, so the count is four below
 * kNonContentArchiveEntries. What remains is the two registry artifacts (the
 * hive and the isolation file) and the isolation file of the filesystem, of
 * the network and of the environment workspace. The patch run and the
 * extraction of a `Build and Run` run of a standalone archive report the same
 * total for their own kind, so both count a constant instead of a literal.
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
 * @brief Get the archive entry name of the launcher program.
 *
 * The launcher is named after the first startup file of the model, so the
 * extracted archive looks like the packaged application: an entry program
 * named `foo.exe` (optionally inside a subdirectory of the imported folder)
 * produces the launcher entry `foo.exe`. The launcher resolves its configuration
 * as `<own file name>.json` beside itself, so the archive entry of the
 * configuration is `<launcher entry>.json`, e.g. `foo.exe.json`.
 *
 * @param[in] model The pack model.
 * @return The file name of the launcher entry, empty when the model has no
 *         startup file.
 */
std::wstring LauncherEntryName(const PackModel& model);

/**
 * @brief Payloads a pack run writes into the archive beside the resources.
 *
 * A standalone archive carries the launcher program and the two sandbox
 * injection modules of the build which produced it. The packer reads them from
 * its own resources (see `src/core/EmbeddedResource.hpp`), so they reach the
 * run as byte ranges instead of file paths; the caller which read them keeps
 * them alive for the whole run.
 *
 * A patch package carries none of the three, so a run of `PackPatch()` needs
 * no payload at all.
 */
struct PackPayloads
{
    const void* launcher_bytes = nullptr;  /*!< The launcher program. */
    std::size_t launcher_size = 0;         /*!< Size of the launcher program in bytes. */
    const void* sandbox32_bytes = nullptr; /*!< The 32 bit sandbox injection module. */
    std::size_t sandbox32_size = 0;        /*!< Size of the 32 bit module in bytes. */
    const void* sandbox64_bytes = nullptr; /*!< The 64 bit sandbox injection module. */
    std::size_t sandbox64_size = 0;        /*!< Size of the 64 bit module in bytes. */
};

/**
 * @brief Pack the model into a self-contained zip archive.
 *
 * The archive layout is the fixed convention of `common/SandboxLayout.hpp`:
 * the read-only resources of the packaged application travel below `app`, one
 * directory per isolation domain, while the `data` directory of the sandbox
 * does not travel at all. The launcher creates it at run time, next to `app`, so
 * deleting it resets the sandbox to the state the archive carries.
 *
 * ```
 * <first startup file name>              launcher payload (payloads.launcher_bytes)
 * <first startup file name>.json         startups[] = { trigger, auto_start,
 *                                        executable = <layer key>\<import>\<exe> }
 * app/sandbox32.dll                      injected sandbox DLL (32 bit)
 * app/sandbox64.dll                      injected sandbox DLL (64 bit)
 * app/filesystem/isolation.json          isolation modes of the filesystem
 * app/filesystem/<layer key>/<import>/... imported folder content
 * app/filesystem/<layer key>/<target>/<file> imported file content
 * app/registry/user.hiv                  virtual registry of the workspace
 * app/registry/isolation.json            isolation modes of the registry
 * app/network/isolation.json             network configuration of the workspace
 * app/environment/isolation.json         environment variables of the workspace
 * ```
 *
 * The launcher program and its configuration carry the file name of the first
 * startup file, see LauncherEntryName(). The entry programs themselves keep
 * their place below the layer tree.
 *
 * The two sandbox injection modules are resources of the archive: they land
 * directly below `app`, beside the four isolation domains, and the launcher
 * injects them from there instead of writing a copy into its state directory,
 * so a run of the extracted archive copies no module at all (see
 * `common/SandboxLayout.hpp`). A patch package carries none of them: the
 * modules belong to the archive which is started and not to the resources a
 * package overrides.
 *
 * The registry artifacts land in the registry domain of the resources: the
 * hive holds the virtual registry the packaged application sees and the
 * isolation file holds the modes which decide which host entries stay visible
 * (see `common/RegistryIsolation.hpp`). The launcher seeds the hive into its
 * state directory before the sandbox mounts it, because mounting a hive writes
 * to the file and the resources below `app` stay read-only.
 *
 * The launcher program also carries the file icon of the first startup file:
 * the icon group which the shell shows for that program is appended to the
 * launcher payload (see ApplyApplicationIcon()), so Explorer shows the icon of
 * the packaged application for the extracted program while the launcher keeps
 * its own icon resources. A startup file without an icon leaves the payload
 * unchanged; the run then only logs a warning instead of failing.
 *
 * The launcher program carries the file properties of the packaged application
 * as well: the version resource of the program the metadata of the session
 * inherits from is written into the payload (see
 * `src/core/ApplicationMetadata.hpp`), with the fields the user edited applied
 * on top of it. The information is read while the run is going on, so a source
 * program which was updated since the project was saved is picked up. A
 * session without a source program, or with one whose version resource cannot
 * be read, writes the fields the user edited only, and a payload which cannot
 * be patched leaves the run a warning instead of a failure.
 *
 * The payloads are supplied by the caller so unit tests can inject fake bytes
 * without a real launcher binary and without real sandbox modules.
 *
 * The progress total covers the files of the imported folders plus the
 * individually imported files and kNonContentArchiveEntries, so the callback
 * receives a stable upper bound for the whole run. The launcher payload and its
 * configuration are reported as the preparing stage before the first imported
 * file is packed.
 *
 * Every report names the file which is being packed through
 * BuildProgress::current, using the path below the import root prefixed by the
 * import name, e.g. `L"MyApp\bin\tool.exe"`.
 *
 * The filesystem isolation modes land in the filesystem domain as
 * `app/filesystem/isolation.json`, next to the layers they describe: the
 * launcher hands the file to the sandbox, which redirects the filesystem of the
 * packaged application through the modes (see
 * `common/FilesystemIsolation.hpp`). The launcher skips the file while it
 * enumerates the layers of that folder.
 *
 * The network configuration of the workspace lands in the network domain as
 * `app/network/isolation.json`: the launcher hands the file to the sandbox,
 * which answers a name resolution of the packaged application from its DNS
 * redirections and sends its traffic through its proxy (see
 * `common/NetworkIsolation.hpp`).
 *
 * The environment variables of the workspace land in the environment domain as
 * `app/environment/isolation.json`: the launcher hands the file to the sandbox,
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
 * @param[in] metadata File properties of the launcher, which are read from the
 *                     program the session inherits them from and written into
 *                     the launcher payload.
 * @param[in] payloads Embedded launcher program and sandbox injection modules,
 *                     written beside the resources of the archive.
 * @param[in] zip_path Destination zip path (truncated when it exists).
 * @param[in] progress Called once per packed file; returning false aborts the
 *                     pack with kBuildCancelledError. May be empty to disable
 *                     progress reporting.
 * @return Error description, empty on success.
 */
std::string Pack(const PackModel& model, const RegistryModel& registry, const FilesystemIsolationModel& isolation,
                 const NetworkModel& network, const EnvironmentModel& environment, const ApplicationMetadata& metadata,
                 const PackPayloads& payloads, const std::wstring& zip_path, const BuildProgressCallback& progress);

/**
 * @brief Pack the resources of the model into a patch package.
 *
 * The archive holds the very same resource tree a standalone archive keeps
 * below `app`, rooted at the archive root instead: the launcher program, its
 * configuration, the two sandbox injection modules and the `app` directory
 * itself do not travel. The package is
 * meant to be dropped into the `patch` directory next to the launcher of a
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
 * package carries no launcher which could start them, so the model of a patch
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
