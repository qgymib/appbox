#ifndef APPBOX_LAUNCHER_UTILS_SANDBOX_MODULES_HPP
#define APPBOX_LAUNCHER_UTILS_SANDBOX_MODULES_HPP

#include <string>

namespace appbox
{

/**
 * @brief Paths of the sandbox injection modules of one run.
 *
 * The launcher carries both modules as RCDATA resources of its own executable
 * and releases them into the cache directory of the sandbox before it creates
 * the runtime of the run. The paths are absolute and name the files the
 * launcher injects into the processes it starts.
 */
struct SandboxModulePaths
{
    std::wstring sandbox32_dll; /*!< The released 32 bit module. */
    std::wstring sandbox64_dll; /*!< The released 64 bit module. */
};

/**
 * @brief Release the embedded sandbox injection modules into a directory.
 *
 * The two modules are resources of the launcher executable (see
 * `launcher/EmbeddedResourceIds.h` and `cmake/LauncherResources.rc.in`). The
 * call creates `cache_dir` and writes both files with `CREATE_ALWAYS`, so a
 * file left by an earlier run is replaced instead of being reused. No digest
 * and no comparison of the content is performed: the modules are small and the
 * copy is fast.
 *
 * A launcher may run beside another one whose processes already loaded the
 * modules, and Windows refuses to truncate an image which is still loaded. When
 * that sharing conflict is the only failure and the file which is already there
 * has the size of the embedded module, the existing file is used and a warning
 * is logged. Every other failure is reported, so the caller can refuse the run
 * before it starts anything.
 *
 * @param[in] cache_dir Directory the modules are written into.
 * @param[out] modules Paths of the released modules, set when the call
 *                     succeeds.
 * @param[out] error Description of the failure, empty when the call succeeds.
 * @return true when both modules are in place.
 */
bool ReleaseSandboxModules(const std::wstring& cache_dir, SandboxModulePaths& modules, std::string& error);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_SANDBOX_MODULES_HPP
