#ifndef APPBOX_TEST_UTILS_LOADER_PATH_HPP
#define APPBOX_TEST_UTILS_LOADER_PATH_HPP

#include <string>
#include "Test.hpp"

namespace appbox::test
{

/**
 * @brief Get the path of the loader executable under test.
 *
 * The path is the one the run was started with: `ctest` passes
 * `--loader=<path>` to both test entries of the single test executable, and
 * the environment variable `APPBOX_TEST_LOADER` is the fallback of a run which
 * is started by hand. The unit tests which work on the real loader payload
 * need the path; they skip themselves when it is empty while every other test
 * keeps running.
 *
 * @return The loader path, empty when it was not provided.
 */
inline const std::wstring& LoaderPath()
{
    return config.loader_path;
}

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_LOADER_PATH_HPP
