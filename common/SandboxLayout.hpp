#ifndef APPBOX_COMMON_SANDBOX_LAYOUT_HPP
#define APPBOX_COMMON_SANDBOX_LAYOUT_HPP

namespace appbox
{

/**
 * @brief Layout of a packed application and of the sandbox it runs in.
 *
 * A packed archive carries the loader program, its configuration and the
 * read-only resources of the packaged application below `app`. The writable
 * state of the sandbox never travels in the archive: the loader creates the
 * `data` directory at run time, next to `app`, so deleting it resets the
 * sandbox to the state the archive was packed with.
 *
 * ```
 * <startup>.exe                        loader payload
 * <startup>.exe.json                   loader configuration
 * app/sandbox32.dll                    injected sandbox DLL (32 bit)
 * app/sandbox64.dll                    injected sandbox DLL (64 bit)
 * app/filesystem/isolation.json        isolation modes of the filesystem
 * app/filesystem/<layer key>/...       imported content (read-only layers)
 * app/registry/user.hiv                virtual registry of the workspace
 * app/registry/isolation.json          isolation modes of the registry
 * app/network/isolation.json           DNS redirections and proxy
 * app/environment/isolation.json       environment variables of the workspace
 * ```
 *
 * A patch package carries the very same resources without the program which
 * starts them: the loader program, its configuration, the two sandbox
 * injection modules and the `app` directory itself do not travel, so the tree
 * of `app` is rooted at the archive root instead. The
 * package is dropped into the `patch` directory next to the loader of a
 * standalone archive, which merges every patch of that directory in ascending
 * name order on top of the resources of `app`.
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
 * Everything below `app` is read-only while the sandbox runs, and the loader
 * only creates the writable state directory `data` at run time:
 *
 * ```
 * data/filesystem/...                  upper layer of the sandbox
 * data/registry/user.hiv               hive the sandbox mounts, seeded from
 *                                      `app/registry/user.hiv` on first run
 * data/environment/state.json          environment variables the packaged
 *                                      application changed inside the sandbox
 * ```
 *
 * A patch package is consumed next to those directories: the user creates
 * `patch` and drops the packages into it, the loader extracts a package into
 * `cache` to reuse the extraction of the runs which follow.
 *
 * ```
 * patch/00-foo.zip                     patch package, created by the user
 * cache/00-foo/filesystem/...          extracted package, created by the loader
 * cache/00-foo/md5.txt                 digest of the extracted package
 * ```
 *
 * Neither directory travels in the archive, and deleting `cache` only costs
 * the extraction of the next run.
 *
 * The names live in `common/` because the packer, the loader and the tests
 * share them: the packer builds the entry names of the archive, the loader
 * resolves the paths below the directory of its configuration file, and the
 * tests materialize the same layout on disk.
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
 * @brief Name of the 32 bit sandbox injection module below `app`.
 *
 * The module is a resource of the archive and not a state of the sandbox: it
 * travels in `app` and the loader injects it from there, so a run of the
 * extracted archive copies no module of its own.
 */
inline constexpr const char* kSandbox32DllName = "sandbox32.dll";

/**
 * @brief Name of the 64 bit sandbox injection module below `app`.
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
 * to it while it runs; it is created by the loader, which owns the state
 * directory, from the state document the sandbox sends over the RPC pipe.
 */
inline constexpr const char* kEnvironmentStateFileName = "state.json";

/**
 * @brief Name of the directory which carries the patch packages.
 *
 * The directory does not travel in the archive: the user of a packaged
 * application creates it next to the loader of a standalone archive and drops
 * the patch packages into it. A package takes effect while it is inside that
 * directory; the packages are applied in ascending name order, so a later
 * package overrides an earlier one.
 */
inline constexpr const char* kPatchDirName = "patch";

/**
 * @brief Name of the directory which carries the extracted patch packages.
 *
 * The directory is created by the loader as soon as the patch directory holds
 * at least one package. Every package has a directory of its own inside it,
 * which holds the extracted resources and the digest of the package they were
 * extracted from.
 */
inline constexpr const char* kCacheDirName = "cache";

/**
 * @brief Name of the file which records the digest of a cache entry.
 *
 * The file is the last entry the loader writes into the directory of a
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
 * very same entry; the duplicates exist because the loader and the archive
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
 * @brief Hive the sandbox mounts, relative to the directory of the loader.
 *
 * The file does not travel in the archive: the loader seeds it from
 * `kRegistryHiveRelative` on the first run and keeps every modification the
 * sandboxed process makes afterwards.
 */
inline constexpr const char* kStateRegistryHiveRelative = "data/registry/user.hiv";

/**
 * @brief Environment variables the packaged application changed, relative to
 *        the directory of the loader.
 *
 * The file does not travel in the archive: the sandbox creates it while the
 * application changes its environment and reads it back on the next run, so a
 * change survives the process which made it. Deleting the state directory
 * resets the environment of the sandbox to the state of the archive.
 */
inline constexpr const char* kStateEnvironmentRelative = "data/environment/state.json";

} // namespace layout

} // namespace appbox

#endif // APPBOX_COMMON_SANDBOX_LAYOUT_HPP
