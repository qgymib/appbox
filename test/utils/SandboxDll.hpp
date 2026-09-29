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
 * The modules are resources of a packed archive and the loader injects them
 * from the resource root instead of writing a copy into its state directory,
 * so a case which starts the loader has to put the real modules into the
 * resource root of its directory. The paths are the ones the run was started
 * with: `ctest` passes `--sandbox32=` and `--sandbox64=`, and the environment
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
 * A run without them cannot start the loader, so the cases which need the
 * sandbox skip themselves instead of failing: see
 * `appbox::test::CommonFixture::SetUp()`.
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
