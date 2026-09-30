#ifndef APPBOX_TEST_UTILS_LAUNCHER_PATH_HPP
#define APPBOX_TEST_UTILS_LAUNCHER_PATH_HPP

#include <string>
#include "Test.hpp"

namespace appbox::test
{

/**
 * @brief Get the path of the launcher executable under test.
 *
 * The path is the one the run was started with: `ctest` passes
 * `--launcher=<path>` to both test entries of the single test executable, and
 * the environment variable `APPBOX_TEST_LAUNCHER` is the fallback of a run which
 * is started by hand. The unit tests which work on the real launcher payload
 * need the path; they skip themselves when it is empty while every other test
 * keeps running.
 *
 * @return The launcher path, empty when it was not provided.
 */
inline const std::wstring& LauncherPath()
{
    return config.launcher_path;
}

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_LAUNCHER_PATH_HPP
