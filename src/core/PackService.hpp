#ifndef APPBOX_PACKER_CORE_PACK_SERVICE_HPP
#define APPBOX_PACKER_CORE_PACK_SERVICE_HPP

#include "BuildReport.hpp"
#include "PackModel.hpp"
#include "RegistryModel.hpp"
#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Number of archive entries which do not come from an import.
 *
 * The loader payload, the loader configuration and the two registry artifacts
 * (the hive and the isolation file). The pack run and the extraction of a
 * `Build and Run` run report the same total, so both count this constant
 * instead of a literal.
 */
inline constexpr std::size_t kNonContentArchiveEntries = 4;

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
 * @brief Get the archive entry name of the loader program.
 *
 * The loader is named after the main program selected in the startup file
 * dialog, so the extracted archive looks like the packaged application: an
 * entry program named `foo.exe` (optionally inside a subdirectory of the
 * imported folder) produces the loader entry `foo.exe`. The loader resolves
 * its configuration as `<own file name>.json` beside itself, so the archive
 * entry of the configuration is `<loader entry>.json`, e.g. `foo.exe.json`.
 *
 * @param[in] model The pack model.
 * @return The file name of the loader entry, empty when the model has no main
 *         program.
 */
std::wstring LoaderEntryName(const PackModel& model);

/**
 * @brief Pack the model into a self-contained zip archive.
 *
 * The archive layout matches the loader runtime conventions:
 *
 * ```
 * <main program name>                    loader payload (loader_bytes)
 * <main program name>.json               base_fs = ["."], overlay_fs = "data",
 *                                        launch.executable = <layer key>\<import>\<exe>
 * data/registry/user.hiv                 virtual registry of the workspace
 * data/registry/isolation.json           isolation modes of the workspace
 * filesystem/<layer key>/<import>/...    imported folder content
 * filesystem/<layer key>/<target>/<file> imported file content
 * ```
 *
 * The loader program and its configuration carry the file name of the main
 * program, see LoaderEntryName(). The entry program itself keeps its place
 * below the layer tree.
 *
 * The registry artifacts land in the overlay folder (`overlay_fs`), which is
 * where the loader mounts the private hive of the sandbox: the hive holds the
 * virtual registry the packaged application sees and the isolation file holds
 * the modes which decide which host entries stay visible (see
 * `common/RegistryIsolation.hpp`).
 *
 * The loader program also carries the file icon of the main program: the icon
 * group which the shell shows for the main program is appended to the loader
 * payload (see ApplyApplicationIcon()), so Explorer shows the icon of the
 * packaged application for the extracted program while the loader keeps its
 * own icon resources. A main program without an icon leaves the payload
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
 * @param[in] model The pack model.
 * @param[in] registry Virtual registry of the workspace, which is written into
 *                     the overlay of the archive as a hive file and an
 *                     isolation file.
 * @param[in] loader_bytes Embedded AppBoxLoader.exe payload.
 * @param[in] loader_size Payload size in bytes.
 * @param[in] zip_path Destination zip path (truncated when it exists).
 * @param[in] progress Called once per packed file; returning false aborts the
 *                     pack with kBuildCancelledError. May be empty to disable
 *                     progress reporting.
 * @return Error description, empty on success.
 */
std::string Pack(const PackModel& model, const RegistryModel& registry, const void* loader_bytes,
                 std::size_t loader_size, const std::wstring& zip_path, const BuildProgressCallback& progress);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PACK_SERVICE_HPP
