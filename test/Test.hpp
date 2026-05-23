#ifndef APPBOX_TEST_HPP
#define APPBOX_TEST_HPP

#include <string>
#include <CLI/CLI.hpp>

namespace appbox::test
{

struct TestConfig
{
    std::wstring loader_path;         /* Path to loader */
    std::wstring log_level = L"info"; /* Log level */
    bool         no_cleanup = false;  /* Do not cleanup the test directory */
};

/**
 * @brief The configuration of the test.
 */
extern TestConfig config;

/**
 * @brief Setup the configuration of the test.
 * @param[in] app The CLI app.
 * @return 0 on success, otherwise a non-zero value.
 */
int SetupTestConfig(CLI::App& app);

} // namespace appbox::test

#endif // APPBOX_TEST_HPP
