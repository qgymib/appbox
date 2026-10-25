#ifndef APPBOX_TEST_UTILS_SANDBOX_DLL_HPP
#define APPBOX_TEST_UTILS_SANDBOX_DLL_HPP

#include <filesystem>
#include <string>
#include "Test.hpp"

namespace appbox::test
{

/**
 * @brief Get the path of the 32 bit sandbox injection module under test.
 *
 * The modules are build products which the launcher embeds as resources of its
 * own executable, so an end-to-end case needs no module file beside it. The
 * paths are still the input of the unit tests which compare the embedded
 * resources with the files of the build tree and which check the runtime
 * libraries of every product. They are the ones the run was started with:
 * `ctest` passes `--sandbox32=` and `--sandbox64=`, and the environment
 * variables `APPBOX_TEST_SANDBOX32` and `APPBOX_TEST_SANDBOX64` are the
 * fallback of a run which is started by hand.
 *
 * @return The module path, empty when it was not provided.
 */
inline const std::wstring& Sandbox32DllPath()
{
    return config.sandbox32_path;
}

/**
 * @brief Get the path of the 64 bit sandbox injection module under test.
 * @return The module path, empty when it was not provided.
 */
inline const std::wstring& Sandbox64DllPath()
{
    return config.sandbox64_path;
}

/**
 * @brief Whether the injection modules of a run are available.
 *
 * A test which compares the embedded payloads with the files of the build tree
 * skips itself when they are unavailable, see `appbox::test::StaticRuntime` and
 * `Unit_EmbeddedResource`.
 *
 * @return true when both modules were provided and exist.
 */
inline bool SandboxModulesAvailable()
{
    if (config.sandbox32_path.empty() || config.sandbox64_path.empty())
    {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(config.sandbox32_path, ec))
    {
        return false;
    }

    ec.clear();
    return std::filesystem::is_regular_file(config.sandbox64_path, ec);
}

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_SANDBOX_DLL_HPP
