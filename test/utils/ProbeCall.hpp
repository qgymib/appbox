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
 * @brief The bit size of the probe process of a case.
 *
 * The probe is a program of its own which is built for both bit sizes, so a
 * case chooses the kind of process its operations run in. A case which does not
 * name a bit size runs the 64 bit probe, which is the probe the suite used
 * before the 32 bit one existed.
 */
enum class ProbeBitness
{
    Bit64, /*!< The 64 bit probe, the probe of a case by default. */
    Bit32  /*!< The 32 bit probe, for a case which checks a 32 bit process. */
};

/**
 * @brief Name of the directory a case collects the log files of a run in.
 *
 * The directory lives inside the working directory of the case, so the log
 * files of a case never travel into the directory of the launcher program and
 * `ReportSandboxLogs()` finds them.
 */
inline constexpr const wchar_t* kCaseLogDirName = L"log";

/**
 * @brief Log files a case asks the launcher of a run for.
 *
 * A run of a case enables the log of the launcher and of every process of its
 * sandbox by default, which is what a reader of the artifact of a failed case
 * needs. A case which checks the log options of the launcher itself leaves an
 * option out or changes its value.
 */
struct LogRun
{
    /**
     * @brief Whether the run passes `--X-AppBox-LogLevel`.
     *
     * A run without the option names no level at all, which is the run which
     * writes no log file.
     */
    bool pass_level = true;

    /**
     * @brief Value of `--X-AppBox-LogLevel`, empty to use the level of the run.
     *
     * The level of the run is the one the test executable was started with
     * (`--log-level`), which is the level of the log files of a case.
     */
    std::wstring level;

    /**
     * @brief Whether the run passes the option which names the log directory.
     */
    bool pass_dir = true;

    /**
     * @brief Value of the log directory option, empty to use the directory of the case.
     *
     * An empty value names the `log` directory of the working directory of the
     * case, which is the directory `ReportSandboxLogs()` reports. A case which
     * checks the default directory of the option or the resolution of a
     * relative path passes the value it tests.
     */
    std::wstring dir;
};

/**
 * @brief Call a probe function.
 * @param[in] name The name of the probe function, encoding in UTF-8.
 * @param[in] data The data to pass to the probe function.
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @param[in] log The log options of the run, see LogRun.
 * @param[in] bitness The bit size of the probe process which runs the function,
 *                    see ProbeBitness.
 * @return The result of the probe function.
 * @throws std::runtime_error The probe executable of the bit size is
 *                            unavailable or the run of the launcher fails.
 */
nlohmann::json ProbeCall(const std::string& name, const nlohmann::json& data, const std::wstring& cwd,
                         const LauncherConfig& launcher_config, const LogRun& log = {},
                         ProbeBitness bitness = ProbeBitness::Bit64);

/**
 * @brief One startup file the launcher started during one run.
 *
 * The report is what the probe process of the startup file sent, so it
 * describes the process the launcher created and not the configuration: the
 * process id, the process which created it and the image it runs are the facts
 * a case checks when it pins how the launcher starts a program.
 */
struct StartupReport
{
    /** Marker of the startup file which started the process. */
    std::string marker;

    /** Process id of the created process. */
    std::uint32_t pid = 0;

    /** Process id of the process which created it. */
    std::uint32_t parent_pid = 0;

    /** Path of the image the process runs, in UTF-8. */
    std::string module_path;
};

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
     * @brief Reports of the started startup files, in the order they arrived.
     */
    std::vector<StartupReport> reports;

    /**
     * @brief Process id of the launcher which ran the configuration.
     *
     * The id is the one of the process the case started, so a case can check
     * which process created the startup files of the run.
     */
    std::uint32_t launcher_pid = 0;

    /**
     * @brief Exit code of the launcher.
     */
    std::uint32_t exit_code = 0;
};

/**
 * @brief Run the launcher of a configuration and report the startup files it
 *        started.
 *
 * Every startup file of the configuration is started as the 64 bit probe
 * process of the run, so every started file reports the marker of its own
 * arguments. The call waits for the launcher, which waits for every file it
 * started, so the reported list is complete when the call returns.
 *
 * The executable of a startup file is replaced by the 64 bit probe, like
 * ProbeCall() does: the paths of a packaged configuration point into the
 * sandbox view, which does not exist on the test machine.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
 *                    the launcher start the auto start files.
 * @param[in] startup_screen Whether the run shows the startup screen. The
 *                           cases turn it off by default, because a window
 *                           which pops up on the desktop of the machine would
 *                           cover the work of the user; the cases which check
 *                           the screen turn it on again.
 * @param[in] log The log options of the run, see LogRun.
 * @param[in] use_configured_executable Whether the run starts the executable
 *                    the configuration spells instead of the probe. A case
 *                    which checks how the launcher locates the image of a
 *                    startup file sets it, because replacing the executable
 *                    would replace the very path the case describes.
 * @param[in] launcher_path Program to run instead of the launcher of the test
 *                    configuration, empty to run the configured launcher. A
 *                    case which verifies a resource of the launcher itself
 *                    passes a patched copy of it.
 * @return The reports of the started startup files and the exit code of the
 *         launcher.
 */
StartupRun ProbeStartupRun(const std::wstring& cwd, const LauncherConfig& launcher_config, const std::string& trigger,
                           bool startup_screen = false, const LogRun& log = {}, bool use_configured_executable = false,
                           const std::wstring& launcher_path = std::wstring());

/**
 * @brief Run a launcher program whose configuration is embedded in its image.
 *
 * The program is started without `--X-AppBox-ConfigFile`, so the run reads the
 * configuration resource of its own executable, which is the way a packed
 * archive runs. The probe arguments are passed as the remaining command line of
 * the launcher, which appends them to the arguments of every startup file.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_path Path of the launcher program to run.
 * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
 *                    the launcher start the auto start files.
 * @param[in] marker Marker passed to the probe processes, empty to omit it.
 * @param[in] log The log options of the run, see LogRun.
 * @return The reports of the started startup files and the exit code of the
 *         launcher.
 */
StartupRun ProbeStartupRunEmbedded(const std::wstring& cwd, const std::wstring& launcher_path,
                                   const std::string& trigger = "", const std::string& marker = "",
                                   const LogRun& log = {});

/**
 * @brief Run a launcher program with an explicit configuration file.
 *
 * The program is started with `--X-AppBox-ConfigFile`, which overrides the
 * configuration embedded in its image. The call exists for the cases which pin
 * that the override still wins over the embedded configuration.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_path Path of the launcher program to run.
 * @param[in] launcher_config The launcher configuration of the run.
 * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
 *                    the launcher start the auto start files.
 * @param[in] log The log options of the run, see LogRun.
 * @return The reports of the started startup files and the exit code of the
 *         launcher.
 */
StartupRun ProbeStartupRunConfigFile(const std::wstring& cwd, const std::wstring& launcher_path,
                                     const LauncherConfig& launcher_config, const std::string& trigger = "",
                                     const LogRun& log = {});

/**
 * @brief Result of a run which starts the programs of its configuration.
 */
struct ProgramRun
{
    /**
     * @brief Exit code of the launcher.
     */
    std::uint32_t exit_code = 0;
};

/**
 * @brief Run the launcher of a configuration without replacing its programs.
 *
 * The executable of every startup file is kept as the configuration spells it,
 * which is what a case needs to check how the launcher starts a program of its
 * own: ProbeStartupRun() points every startup file at the 64 bit probe, which
 * would hide the kind of the file.
 *
 * The remaining arguments of the launcher are left out as well, because the
 * launcher appends them to the arguments of every startup file. The console
 * window of a started program is hidden and the startup screen is turned off,
 * like every other run of a case.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @param[in] log The log options of the run, see LogRun.
 * @return The exit code of the launcher.
 */
ProgramRun ProbeProgramRun(const std::wstring& cwd, const LauncherConfig& launcher_config, const LogRun& log = {});

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
 * The startup files of the configuration are pointed at the 64 bit probe, like
 * ProbeStartupRun() does, so a report of a probe proves that the launcher
 * started a startup file although the shell replaces the application of the
 * configuration. The call does not wait for a report: the
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
 * @param[in] log The log options of the run, see LogRun.
 * @return The markers the probes reported and the exit code of the launcher.
 */
ShellRun ProbeShellRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                       const std::vector<std::string>& shell_command, const std::string& trigger = "",
                       const LogRun& log = {});

} // namespace appbox::test

#endif
