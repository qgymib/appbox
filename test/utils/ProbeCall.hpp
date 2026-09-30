#ifndef APPBOX_TEST_PROBE_CALL_HPP
#define APPBOX_TEST_PROBE_CALL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "launcher/Config.hpp"

namespace appbox::test
{

struct ProbeRequest
{
    static constexpr char* Method = "ProbeRequest";

    struct Req
    {
        std::string key;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, key)
    };

    struct Rsp
    {
        std::string    name;
        nlohmann::json data;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, name, data)
    };
};

struct ProbeResponse
{
    static constexpr char* Method = "ProbeResponse";

    struct Req
    {
        std::string    key;
        nlohmann::json result;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, key, result)
    };

    struct Rsp
    {
        int _;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, _)
    };
};

/**
 * @brief Call a probe function.
 * @param[in] name The name of the probe function, encoding in UTF-8.
 * @param[in] data The data to pass to the probe function.
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @return The result of the probe function.
 */
nlohmann::json ProbeCall(const std::string& name, const nlohmann::json& data, const std::wstring& cwd,
                         const LauncherConfig& launcher_config);

/**
 * @brief Startup files the launcher started during one run.
 */
struct StartupRun
{
    /**
     * @brief Markers of the startup files which were started, in name order.
     */
    std::vector<std::string> started;

    /**
     * @brief Exit code of the launcher.
     */
    std::uint32_t exit_code = 0;
};

/**
 * @brief Run the launcher of a configuration and report the startup files it
 *        started.
 *
 * Every startup file of the configuration is started as the probe process of
 * this executable, so every started file reports the marker of its own
 * arguments. The call waits for the launcher, which waits for every file it
 * started, so the reported list is complete when the call returns.
 *
 * The executable of a startup file is replaced by this executable, like
 * ProbeCall() does: the paths of a packaged configuration point into the
 * sandbox view, which does not exist on the test machine.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
 *                    the launcher start the auto start files.
 * @return The markers of the started startup files and the exit code of the
 *         launcher.
 */
StartupRun ProbeStartupRun(const std::wstring& cwd, const LauncherConfig& launcher_config, const std::string& trigger);

/**
 * @brief Startup files the launcher started during one run of the shell.
 */
struct ShellRun
{
    /**
     * @brief Markers of the probe processes which reported during the run, in
     *        name order.
     *
     * A run of the shell starts no startup file, so a marker proves that the
     * launcher started a startup file it was asked to ignore.
     */
    std::vector<std::string> reported;

    /**
     * @brief Exit code of the launcher.
     */
    std::uint32_t exit_code = 0;
};

/**
 * @brief Run the launcher of a configuration with the shell of the host.
 *
 * The command is handed over as the remaining command line of the launcher,
 * which the launcher turns into `cmd /c <command>` and runs inside the sandbox
 * of the configuration.
 *
 * The startup files of the configuration are pointed at the probe process of
 * this executable, like ProbeStartupRun() does, so a report of a probe proves
 * that the launcher started a startup file although the shell replaces the
 * application of the configuration. The call does not wait for a report: the
 * shell runs the command of the case instead of the probe, and the markers
 * which arrived until the launcher left are collected afterwards.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @param[in] shell_command Command the shell runs, in UTF-8; empty to run the
 *                          shell itself, which is not usable in a case because
 *                          an interactive shell waits for input.
 * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to omit
 *                    the option.
 * @return The markers the probes reported and the exit code of the launcher.
 */
ShellRun ProbeShellRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                       const std::vector<std::string>& shell_command, const std::string& trigger = "");

} // namespace appbox::test

#endif
