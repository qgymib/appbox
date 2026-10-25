#ifndef APPBOX_COMMON_SANDBOX_LAYOUT_HPP
#define APPBOX_COMMON_SANDBOX_LAYOUT_HPP

namespace appbox
{

/**
 * @brief Layout of a packed application and of the sandbox it runs in.
 *
 * A packed archive carries the launcher program, which carries the run
 * configuration and, when the session sets one, the startup image as RCDATA
 * resources of its own image, and the read-only resources of the packaged
 * application below `app`. The writable state of the sandbox never travels in
 * the archive: the launcher creates the
 * `data` directory at run time, next to `app`, releases the sandbox injection
 * modules into `data/cache` and keeps the state there, so deleting it resets the
 * sandbox to the state the archive was packed with.
 *
 * ```
 * <startup>.exe                        launcher payload, carries the
 *                                      configuration and the startup image as
 *                                      RCDATA resources
 * app/filesystem/isolation.json        isolation modes of the filesystem
 * app/filesystem/<layer key>/...       imported content (read-only layers)
 * app/registry/user.hiv                virtual registry of the workspace
 * app/registry/isolation.json          isolation modes of the registry
 * app/network/isolation.json           DNS redirections and proxy
 * app/environment/isolation.json       environment variables of the workspace
 * data/cache/sandbox32.dll             released sandbox DLL (32 bit)
 * data/cache/sandbox64.dll             released sandbox DLL (64 bit)
 * data/cache/<package>/...             extracted patch package (optional)
 * ```
 *
 * The launcher carries the two sandbox injection modules as resources of its
 * own executable and releases them into `data/cache` at every start, so neither
 * the archive nor a patch package carries a module file.
 *
 * A patch package carries the very same resources without the program which
 * starts them: the launcher program, the configuration it carries and the `app`
 * directory itself do not travel, so the tree of `app` is rooted at the archive
 * root instead. The package is dropped into the `patch` directory next to the
 * launcher of a standalone archive, which merges every patch of that directory
 * in ascending name order on top of the resources of `app`.
 *
 * The `Standalone (EXE)` product carries the two halves of an archive in one
 * file: the program is the launcher image, the resources below `app` are
 * appended to it as a zip archive and the position and the size of that payload
 * are written into a trailer behind it (see `common/ExecutablePayload.hpp`).
 * The launcher extracts the payload into a temporary folder of the run and takes
 * the `app` directory of that folder as its read-only root, so the tree above
 * describes the content of the payload, while `data`, `patch` and `log` stay
 * beside the program itself.
 *
 * ```
 * filesystem/isolation.json            isolation modes of the filesystem
 * filesystem/<layer key>/...           imported content (read-only layers)
 * registry/user.hiv                    virtual registry of the workspace
 * registry/isolation.json              isolation modes of the registry
 * network/isolation.json               DNS redirections and proxy
 * environment/isolation.json           environment variables of the workspace
 * ```
 *
 * Everything below `app` is read-only while the sandbox runs, and the launcher
 * creates the writable state directory `data` at run time:
 *
 * ```
 * data/filesystem/...                  upper layer of the sandbox
 * data/registry/user.hiv               hive of the sandbox, it holds the
 *                                      modifications of the runs
 * data/registry/image/user.hiv         read-only image of the registry: the
 *                                      content of `app` merged with the patch
 *                                      packages, rebuilt at every start
 * data/environment/state.json          environment variables the packaged
 *                                      application changed inside the sandbox
 * data/cache/sandbox32.dll             released sandbox module (32 bit)
 * data/cache/sandbox64.dll             released sandbox module (64 bit)
 * data/cache/00-foo/...                extracted patch package, see below
 * ```
 *
 * The registry of a run is a view of two sandbox hives and the real registry:
 * the state hive of the sandbox wins over the image, which wins over the host
 * registry. The image carries the read-only layers of the run, which are the
 * resources of the archive and the packages in the order the packages take
 * effect in, so the state of the sandbox is never overwritten by the content of a layer
 * below it, and a rebuilt archive or a replaced package reaches the next run
 * all the same.
 *
 * A patch package is consumed next to those directories: the user creates
 * `patch` and drops the packages into it, the launcher extracts a package into
 * the `cache` directory of the state to reuse the extraction of the runs which
 * follow.
 *
 * ```
 * patch/00-foo.zip                     patch package, created by the user
 * data/cache/00-foo/filesystem/...     extracted package, created by the launcher
 * data/cache/00-foo/md5.txt            digest of the extracted package
 * ```
 *
 * Neither `patch` nor the state directory travels in the archive. The cache
 * lives inside the state, so deleting the state directory drops the sandbox
 * state, the released modules and the extractions at once, while deleting only
 * `data/cache` costs the release and the extraction of the next run.
 *
 * The names live in `common/` because the packer, the launcher and the tests
 * share them: the packer builds the entry names of the archive, the launcher
 * resolves the paths below the directory of its program, and the tests
 * materialize the same layout on disk.
 */
namespace layout
{

/**
 * @brief Name of the read-only resource directory of the archive.
 */
inline constexpr const char* kAppDirName = "app";

/**
 * @brief Name of the writable state directory, created at run time.
 */
inline constexpr const char* kStateDirName = "data";

/**
 * @brief Name of the filesystem directory below `app`.
 */
inline constexpr const char* kFilesystemDirName = "filesystem";

/**
 * @brief Name of the registry directory below `app`.
 */
inline constexpr const char* kRegistryDirName = "registry";

/**
 * @brief Name of the network directory below `app`.
 */
inline constexpr const char* kNetworkDirName = "network";

/**
 * @brief Name of the environment directory below `app`.
 */
inline constexpr const char* kEnvironmentDirName = "environment";

/**
 * @brief Name of the 32 bit sandbox injection module below `data/cache`.
 *
 * The module is a resource of the launcher executable: the launcher releases it
 * into the cache directory of the sandbox at every start and injects it from
 * there, so neither the archive nor a patch package carries the file.
 */
inline constexpr const char* kSandbox32DllName = "sandbox32.dll";

/**
 * @brief Name of the 64 bit sandbox injection module below `data/cache`.
 */
inline constexpr const char* kSandbox64DllName = "sandbox64.dll";

/**
 * @brief Name of the isolation file of every domain directory.
 */
inline constexpr const char* kIsolationFileName = "isolation.json";

/**
 * @brief Name of the hive file which carries the virtual registry.
 */
inline constexpr const char* kRegistryHiveFileName = "user.hiv";

/**
 * @brief Name of the state file which carries the environment of the sandbox.
 *
 * The file lives in the state directory, because the sandboxed process writes
 * to it while it runs; it is created by the launcher, which owns the state
 * directory, from the state document the sandbox sends over the RPC pipe.
 */
inline constexpr const char* kEnvironmentStateFileName = "state.json";

/**
 * @brief Name of the directory which carries the patch packages.
 *
 * The directory does not travel in the archive: the user of a packaged
 * application creates it next to the launcher of a standalone archive and drops
 * the patch packages into it. A package takes effect while it is inside that
 * directory; the packages are applied in ascending name order, so a later
 * package overrides an earlier one.
 */
inline constexpr const char* kPatchDirName = "patch";

/**
 * @brief Name of the directory which carries the extracted patch packages.
 *
 * The directory lives below the state directory, because an extraction is state
 * the launcher creates at run time and not a resource of the archive. It is
 * created as soon as the patch directory holds at least one package. Every
 * package has a directory of its own inside it, which holds the extracted
 * resources and the digest of the package they were extracted from.
 */
inline constexpr const char* kCacheDirName = "cache";

/**
 * @brief Name of the file which records the digest of a cache entry.
 *
 * The file is the last entry the launcher writes into the directory of a
 * package, so its presence marks a complete extraction, and its content is
 * the digest the package had when it was extracted.
 */
inline constexpr const char* kPatchDigestFileName = "md5.txt";

/**
 * @brief Extension of a patch package.
 *
 * Only regular files which carry this extension are read as packages; the
 * comparison ignores the case of the extension.
 */
inline constexpr const char* kPatchPackageExtension = ".zip";

/**
 * @brief The names above, for the consumers which work with wide strings.
 *
 * The names are pure ASCII, so the wide and the narrow spelling describe the
 * very same entry; the duplicates exist because the launcher and the archive
 * reader hold wide paths while the packer builds narrow entry names.
 */
inline constexpr const wchar_t* kAppDirNameW = L"app";

/** @brief Wide spelling of kStateDirName. */
inline constexpr const wchar_t* kStateDirNameW = L"data";

/** @brief Wide spelling of kFilesystemDirName. */
inline constexpr const wchar_t* kFilesystemDirNameW = L"filesystem";

/** @brief Wide spelling of kRegistryDirName. */
inline constexpr const wchar_t* kRegistryDirNameW = L"registry";

/** @brief Wide spelling of kNetworkDirName. */
inline constexpr const wchar_t* kNetworkDirNameW = L"network";

/** @brief Wide spelling of kEnvironmentDirName. */
inline constexpr const wchar_t* kEnvironmentDirNameW = L"environment";

/** @brief Wide spelling of kSandbox32DllName. */
inline constexpr const wchar_t* kSandbox32DllNameW = L"sandbox32.dll";

/** @brief Wide spelling of kSandbox64DllName. */
inline constexpr const wchar_t* kSandbox64DllNameW = L"sandbox64.dll";

/** @brief Wide spelling of kIsolationFileName. */
inline constexpr const wchar_t* kIsolationFileNameW = L"isolation.json";

/** @brief Wide spelling of kRegistryHiveFileName. */
inline constexpr const wchar_t* kRegistryHiveFileNameW = L"user.hiv";

/** @brief Wide spelling of kEnvironmentStateFileName. */
inline constexpr const wchar_t* kEnvironmentStateFileNameW = L"state.json";

/** @brief Wide spelling of kPatchDirName. */
inline constexpr const wchar_t* kPatchDirNameW = L"patch";

/** @brief Wide spelling of kCacheDirName. */
inline constexpr const wchar_t* kCacheDirNameW = L"cache";

/** @brief Wide spelling of kPatchDigestFileName. */
inline constexpr const wchar_t* kPatchDigestFileNameW = L"md5.txt";

/** @brief Wide spelling of kPatchPackageExtension. */
inline constexpr const wchar_t* kPatchPackageExtensionW = L".zip";

/**
 * @brief Root of the read-only layers, relative to the archive root.
 *
 * Every child directory of this folder is a layer of the view and is named
 * after the layer key it maps (`#ProgramFiles#`, a single drive letter, ...).
 *
 * A patch package roots the same folder at the archive root, which is the
 * spelling of kFilesystemDirName.
 */
inline constexpr const char* kLayerRootRelative = "app/filesystem";

/*
 * The names below exist in two spellings. The archive relative one roots the
 * entry below `app` and describes a standalone archive; the resource relative
 * one roots the entry at the resource root, which is `app` inside a standalone
 * archive and the archive root inside a patch package. Both describe the very
 * same entry: the archive relative spelling is kAppDirName, a slash and the
 * resource relative spelling.
 */

/**
 * @brief Isolation modes of the filesystem workspace, relative to the archive
 *        root.
 */
inline constexpr const char* kFilesystemIsolationRelative = "app/filesystem/isolation.json";

/**
 * @brief Isolation modes of the filesystem workspace, relative to the resource
 *        root.
 */
inline constexpr const char* kFilesystemIsolationAppRelative = "filesystem/isolation.json";

/**
 * @brief Virtual registry of the workspace, relative to the archive root.
 */
inline constexpr const char* kRegistryHiveRelative = "app/registry/user.hiv";

/**
 * @brief Virtual registry of the workspace, relative to the resource root.
 */
inline constexpr const char* kRegistryHiveAppRelative = "registry/user.hiv";

/**
 * @brief Isolation modes of the registry workspace, relative to the archive
 *        root.
 */
inline constexpr const char* kRegistryIsolationRelative = "app/registry/isolation.json";

/**
 * @brief Isolation modes of the registry workspace, relative to the resource
 *        root.
 */
inline constexpr const char* kRegistryIsolationAppRelative = "registry/isolation.json";

/**
 * @brief Network configuration of the workspace, relative to the archive root.
 */
inline constexpr const char* kNetworkIsolationRelative = "app/network/isolation.json";

/**
 * @brief Network configuration of the workspace, relative to the resource
 *        root.
 */
inline constexpr const char* kNetworkIsolationAppRelative = "network/isolation.json";

/**
 * @brief Environment variables of the workspace, relative to the archive root.
 */
inline constexpr const char* kEnvironmentIsolationRelative = "app/environment/isolation.json";

/**
 * @brief Environment variables of the workspace, relative to the resource
 *        root.
 */
inline constexpr const char* kEnvironmentIsolationAppRelative = "environment/isolation.json";

/**
 * @brief Hive of the state of the sandbox, relative to the directory of the
 *        launcher.
 *
 * The file does not travel in the archive: it holds the modifications the
 * sandboxed process makes, and it is the highest layer of the registry view of
 * a run. The sandbox creates it when it mounts it, so deleting the state
 * directory resets the registry to the state of the archive and of the patch
 * packages.
 */
inline constexpr const char* kStateRegistryHiveRelative = "data/registry/user.hiv";

/**
 * @brief Read-only image of the registry of the run, relative to the directory
 *        of the launcher.
 *
 * The image is the content of the registry of `app` merged with the hive of
 * every patch package in the order the packages take effect in, and the
 * launcher rebuilds it at every start: the content of the archive and of the
 * packages is therefore applied again while the state of the sandbox stays the
 * highest layer of the view.
 *
 * The file lives in a directory of its own because mounting a hive writes log
 * files next to it, and the whole directory is replaced when the image is
 * rebuilt.
 */
inline constexpr const char* kStateRegistryImageRelative = "data/registry/image/user.hiv";

/**
 * @brief Environment variables the packaged application changed, relative to
 *        the directory of the launcher.
 *
 * The file does not travel in the archive: the sandbox creates it while the
 * application changes its environment and reads it back on the next run, so a
 * change survives the process which made it. Deleting the state directory
 * resets the environment of the sandbox to the state of the archive.
 */
inline constexpr const char* kStateEnvironmentRelative = "data/environment/state.json";

/**
 * @brief Extracted patch packages, relative to the directory of the launcher.
 *
 * The directory lives inside the state directory, so the state of the sandbox
 * and the extractions of its patch packages are reset together: deleting `data`
 * drops both, while deleting only `data/cache` costs the release of the sandbox
 * modules and the extraction of the next run. The launcher creates the directory
 * at every start for the two released sandbox modules, and every patch package
 * owns the entry named after its file name without the extension.
 */
inline constexpr const char* kStateCacheRelative = "data/cache";

} // namespace layout

} // namespace appbox

#endif // APPBOX_COMMON_SANDBOX_LAYOUT_HPP
