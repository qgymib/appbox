#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <spdlog/spdlog.h>
#include "SetLogLevel.hpp"
#include "Test.hpp"

appbox::test::TestConfig appbox::test::config;

/**
 * @brief Get environment variable value
 * @param[in] key Environment variable key
 * @param[out] val Environment variable value
 * @return True if successful, otherwise false
 */
static bool GetEnv(const std::wstring& key, std::wstring& val)
{
    val.clear();
    if (key.empty())
    {
        return false;
    }

    /* Query the required buffer size.*/
    SetLastError(ERROR_SUCCESS);
    DWORD required = ::GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (required == 0)
    {
        /*
         * Check if the environment variable exists.
         * There may be existing environment variables that are empty.
         */
        return ::GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    }

    /* Query the environment variable. */
    std::vector<wchar_t> buffer(required);
    DWORD                written = ::GetEnvironmentVariableW(key.c_str(), buffer.data(), required);

    /* Check if environment variable is changed. */
    if (written == 0 || written >= required)
    {
        return false;
    }

    val.assign(buffer.data(), written);
    return true;
}

static void SetLogLevelFromString(const std::wstring& level)
{
    appbox::test::config.log_level = level;
    appbox::SetLogLevel(level);
}

static void SetConfigFromEnv()
{
    std::wstring val;
    if (GetEnv(L"APPBOX_TEST_LOADER", val))
    {
        appbox::test::config.loader_path = val;
    }
    if (GetEnv(L"APPBOX_TEST_LOG_LEVEL", val))
    {
        SetLogLevelFromString(val);
    }
    if (GetEnv(L"APPBOX_TEST_NO_CLEANUP", val))
    {
        appbox::test::config.no_cleanup = _wcsicmp(val.c_str(), L"0") && _wcsicmp(val.c_str(), L"false") &&
                                          _wcsicmp(val.c_str(), L"no") && _wcsicmp(val.c_str(), L"off");
    }
}

int appbox::test::SetupTestConfig(CLI::App& app)
{
    SetConfigFromEnv();

    app.add_option("--loader", appbox::test::config.loader_path,
                   "Path to loader. Environment variable: APPBOX_TEST_LOADER.");
    app.add_option_function<std::wstring>("--log-level", SetLogLevelFromString,
                                          "Log level. Available levels: trace, debug, info, warn, err, critical, off. "
                                          "Default: info. Environment variable: APPBOX_TEST_LOG_LEVEL.");
    app.add_option("--no-cleanup", appbox::test::config.no_cleanup,
                   "Do not cleanup the test directory. Environment variable: APPBOX_TEST_NO_CLEANUP.");

    return 0;
}
