#ifndef APPBOX_TEST_UNIT_LOADER_PATH_HPP
#define APPBOX_TEST_UNIT_LOADER_PATH_HPP

#include <string>
#include <utility>

namespace appbox::test
{

/**
 * @brief Storage of the loader path the test run was started with.
 * @return The mutable storage.
 */
inline std::wstring& LoaderPathStorage()
{
    static std::wstring path;
    return path;
}

/**
 * @brief Set the path of the loader executable under test.
 * @param[in] path Absolute path of AppBoxLoader.exe.
 */
inline void SetLoaderPath(std::wstring path)
{
    LoaderPathStorage() = std::move(path);
}

/**
 * @brief Get the path of the loader executable under test.
 *
 * The unit tests which work on the real loader payload need the path of
 * AppBoxLoader.exe, which ctest passes with `--loader=<path>`. When the
 * executable is started by hand without the argument, the path is empty and
 * those tests are skipped while every other test keeps running.
 *
 * @return The loader path, empty when it was not provided.
 */
inline const std::wstring& LoaderPath()
{
    return LoaderPathStorage();
}

} // namespace appbox::test

#endif // APPBOX_TEST_UNIT_LOADER_PATH_HPP
