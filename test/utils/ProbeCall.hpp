#ifndef APPBOX_TEST_PROBE_CALL_HPP
#define APPBOX_TEST_PROBE_CALL_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "launcher/Config.hpp"

namespace appbox::test
{

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
 * @brief Budget of one request of a case to a probe, in milliseconds.
 *
 * A probe which does not answer in time is a defect of the probe or of the
 * sandbox it runs in, so the budget is generous: the call throws instead of
 * hanging the case, which is what a `ctest` run needs.
 */
constexpr std::uint32_t kProbeCallTimeoutMs = 30000;

/**
 * @brief Time without a new connection after which the probes of a run are
 *        taken as complete, in milliseconds.
 *
 * The launcher starts the programs of a run one after the other, so the probes
 * connect within a few milliseconds of each other. A run which starts no
 * program at all is told apart by the end of the launcher itself.
 */
constexpr std::uint32_t kProbeBindQuietMs = 300;

/**
 * @brief Budget of the wait for the probes of a run, in milliseconds.
 */
constexpr std::uint32_t kProbeBindTimeoutMs = 30000;

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
 * @brief One launcher run and the probes it started.
 *
 * A run owns the launcher process, the key which names it and the sessions of
 * the probes which connected for that key: a launcher starts one probe per
 * startup file, so one key may own several sessions. The launcher is started on
 * a worker thread, because it returns only when its last program left, and the
 * probes leave when the run asks them to: a case therefore keeps a probe alive
 * by keeping its run alive, and ends it with `ExitAll()`.
 *
 * The object is created by the helpers of a case and not by a case itself; a
 * case which runs one probe uses `Probe`, which owns a run of its own.
 */
class ProbeRun
{
public:
    /**
     * @brief Start one launcher run.
     *
     * @param[in] cwd Working directory of the case, which is also the directory
     *                the launcher resolves its sandbox layout against.
     * @param[in] launcher Function which runs the launcher and returns its exit
     *                code. It receives the key of the run, which the arguments
     *                of the probe processes of the run carry.
     */
    explicit ProbeRun(const std::wstring& cwd, std::function<std::uint32_t(const std::string& key)> launcher);

    ~ProbeRun();

    ProbeRun(const ProbeRun&) = delete;
    ProbeRun& operator=(const ProbeRun&) = delete;

    /**
     * @brief Key which names this run.
     * @return The key of the run.
     */
    const std::string& Key() const;

    /**
     * @brief Number of probes which connected for this run.
     * @return The number of connected probes.
     */
    std::size_t SessionCount() const;

    /**
     * @brief Wait until the probes of the run connected.
     *
     * A run which starts no probe is told apart by its launcher: a launcher
     * which left started no program, and a launcher which left with a non zero
     * code started none either.
     *
     * @param[in] quiet_ms Time without a new connection after which the set of
     *                     probes is taken as complete.
     * @param[in] timeout_ms Budget of the wait.
     * @param[in] wait_for_launcher Whether the end of the launcher ends the
     *                     wait. A run whose programs are started by another
     *                     launcher clears the flag, because it leaves before its
     *                     programs connect.
     * @return true when at least one probe connected.
     */
    bool WaitForSessions(std::uint32_t quiet_ms = kProbeBindQuietMs, std::uint32_t timeout_ms = kProbeBindTimeoutMs,
                         bool wait_for_launcher = true);

    /**
     * @brief Ask every connected probe to leave.
     *
     * The call is idempotent and never throws: a probe which already left is
     * not an error of the case, because the run may have ended on its own.
     *
     * @param[in] code Exit code of the probe processes.
     */
    void ExitAll(std::int32_t code = 0);

    /**
     * @brief Whether the launcher of the run left.
     * @return true when the launcher ended.
     */
    bool Finished() const;

    /**
     * @brief Wait for the end of the launcher run.
     * @return The exit code of the launcher.
     */
    std::uint32_t Wait();

    /**
     * @brief Wait until the launcher of the run left, within a budget.
     *
     * The call reports the end of the launcher alone and not the end of the
     * programs it started, so a caller which has to know how the run ended
     * reads the code with `ExitCode()` afterwards: a launcher of a second
     * instance leaves as soon as the master instance answered its request,
     * which is before the programs of that request report anything.
     *
     * @param[in] timeout_ms Budget of the wait.
     * @return true when the launcher ended, false when the budget was used up.
     */
    bool WaitForLauncher(std::uint32_t timeout_ms);

    /**
     * @brief Exit code of the launcher of the run.
     *
     * The code is the one of the launcher process, so it is only meaningful
     * once the launcher ended, see `Finished()` and `WaitForLauncher()`.
     *
     * @return The exit code of the launcher.
     */
    std::uint32_t ExitCode() const;

    /**
     * @brief Time the launcher of the run worked, in milliseconds.
     *
     * The time covers the start of the launcher process, the request of the
     * case and the answer of the master instance, so a case which pins how fast
     * a request is answered reads it instead of timing its own calls: the time
     * a caller measures covers the programs of the request as well, which start
     * and report after the answer.
     *
     * @return The lifetime of the launcher, zero when it did not end yet.
     */
    std::uint32_t LauncherLifetimeMs() const;

    /**
     * @brief Process id of the first probe of the run.
     * @return The process id, zero when no probe connected.
     */
    std::uint32_t Pid() const;

    /**
     * @brief Send one request to one probe of the run.
     *
     * @tparam Protocol Protocol struct of the probe, see `test/probe/<Name>.hpp`.
     * @param[in] request Request of the probe.
     * @return The answer of the probe.
     * @throws std::runtime_error No probe connected, the probe failed the
     *                            request or it did not answer in time.
     */
    template <typename Protocol>
    typename Protocol::Rsp Call(const typename Protocol::Req& request)
    {
        return CallRaw(Protocol::Method, nlohmann::json(request)).template get<typename Protocol::Rsp>();
    }

    /**
     * @brief Send one request to every probe of the run.
     *
     * A program of a run may be gone: a master instance terminates the programs
     * of a request it has to roll back, so the answers of a run are the ones
     * which arrived. A case which needs every program to answer checks the
     * number of the answers itself, and a case which does not care about a
     * program the sandbox removed is not refused for its silence.
     *
     * @tparam Protocol Protocol struct of the probe, see `test/probe/<Name>.hpp`.
     * @param[in] request Request of the probes.
     * @return The answer of every probe which answered, in the order the probes
     *         connected.
     * @throws std::runtime_error No probe connected to the run.
     */
    template <typename Protocol>
    std::vector<typename Protocol::Rsp> CallAll(const typename Protocol::Req& request)
    {
        std::vector<typename Protocol::Rsp> answers;
        for (const auto& answer : CallAllRaw(Protocol::Method, nlohmann::json(request)))
        {
            answers.push_back(answer.template get<typename Protocol::Rsp>());
        }
        return answers;
    }

    /**
     * @brief Send one request to one probe and return its answer as it arrived.
     * @param[in] method Method of the request.
     * @param[in] params Parameters of the request.
     * @param[in] timeout_ms Budget of the request.
     * @return The result of the answer.
     * @throws std::runtime_error No probe connected, or the request failed.
     */
    nlohmann::json CallRaw(const std::string& method, const nlohmann::json& params,
                           std::uint32_t timeout_ms = kProbeCallTimeoutMs);

    /**
     * @brief Send one request to every probe of the run.
     *
     * A probe which cannot answer is reported in the log of the case and
     * skipped, see `CallAll()`: the programs of a run are not guaranteed to
     * outlive the request which started them.
     *
     * @param[in] method Method of the request.
     * @param[in] params Parameters of the request.
     * @param[in] timeout_ms Budget of every request.
     * @return The result of every answer which arrived, in the order the probes
     *         connected.
     * @throws std::runtime_error No probe connected to the run.
     */
    std::vector<nlohmann::json> CallAllRaw(const std::string& method, const nlohmann::json& params,
                                           std::uint32_t timeout_ms = kProbeCallTimeoutMs);

private:
    struct Data;

    std::shared_ptr<Data> data_;
};

/**
 * @brief Handle of the probes a case started with one launcher run.
 *
 * The handle owns one launcher run, so it owns the probe processes of that run:
 * the probe serves the requests of the case until the handle ends it, which is
 * what lets a case ask one probe process several questions. The destructor
 * ends the run, so a case which returns early still leaves no probe behind.
 */
class Probe
{
public:
    /**
     * @brief Run the launcher of a configuration and serve its probes.
     *
     * The executable of every startup file is replaced by the probe of the bit
     * size, like the helpers of a case do: the paths of a packaged
     * configuration point into the sandbox view, which does not exist on the
     * test machine. A configuration which describes no startup file starts the
     * probe once, like the single main program of a packaged application.
     *
     * @param[in] cwd The current working directory.
     * @param[in] launcher_config The launcher configuration.
     * @param[in] bitness The bit size of the probe processes of the run.
     * @param[in] log The log options of the run, see LogRun.
     * @param[in] marker Marker of the probe processes, empty to omit it.
     * @param[in] args Remaining arguments of the launcher, which it appends to
     *                 the arguments of every startup file.
     * @throws std::runtime_error The probe executable of the bit size is
     *                            unavailable.
     */
    Probe(const std::wstring& cwd, const LauncherConfig& launcher_config, ProbeBitness bitness = ProbeBitness::Bit64,
          const LogRun& log = {}, const std::string& marker = std::string(),
          const std::vector<std::wstring>& args = {});

    ~Probe();

    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;

    /**
     * @brief Send one request to the probe.
     *
     * The call waits for the connection of the probe within the budget of the
     * wait, so a case issues its first request as soon as the process runs.
     *
     * @tparam Protocol Protocol struct of the probe, see `test/probe/<Name>.hpp`.
     * @param[in] request Request of the probe.
     * @return The answer of the probe.
     * @throws std::runtime_error The probe did not connect, failed the request
     *                            or did not answer in time.
     */
    template <typename Protocol>
    typename Protocol::Rsp Call(const typename Protocol::Req& request)
    {
        return run_->Call<Protocol>(request);
    }

    /**
     * @brief Send one request to every probe of the run.
     * @tparam Protocol Protocol struct of the probe, see `test/probe/<Name>.hpp`.
     * @param[in] request Request of the probes.
     * @return The answer of every probe, in the order the probes connected.
     * @throws std::runtime_error A probe failed the request or did not answer
     *                            in time.
     */
    template <typename Protocol>
    std::vector<typename Protocol::Rsp> CallAll(const typename Protocol::Req& request)
    {
        return run_->CallAll<Protocol>(request);
    }

    /**
     * @brief End the probes of the run and wait for the launcher.
     *
     * The call is idempotent, so the destructor may repeat it.
     *
     * @param[in] code Exit code of the probe processes.
     */
    void Exit(std::int32_t code = 0);

    /**
     * @brief Wait for the end of the launcher run.
     * @return The exit code of the launcher.
     */
    std::uint32_t Wait();

    /**
     * @brief Process id of the probe.
     * @return The process id, zero when the probe did not connect.
     */
    std::uint32_t Pid() const;

    /**
     * @brief The run which owns the probe processes.
     * @return The run of the handle.
     */
    ProbeRun& Run();

private:
    std::unique_ptr<ProbeRun> run_;
};

/**
 * @brief Run the launcher of a configuration document and wait for it.
 *
 * The document is written as it is, so a case can describe a configuration the
 * type `LauncherConfig` cannot express: a member which is missing, a member of
 * another type or a value the type accepts but the launcher refuses. The call
 * starts no probe process, because a run which is refused starts no program at
 * all; it reports the exit code of the launcher alone.
 *
 * The name of the file is the one of the case unless the case names another
 * one: two launchers of one sandbox share the directory which carries the
 * sandbox, so the second instance of a case writes its configuration beside the
 * file of the master instance instead of over it.
 *
 * @param[in] cwd The current working directory.
 * @param[in] document The launcher configuration document.
 * @param[in] log The log options of the run, see LogRun.
 * @param[in] config_file_name Name of the file the document is written into.
 * @param[out] launcher_pid Process id of the launcher which was started, may be
 *                          null.
 * @param[in] args Arguments of the launcher, without the options which select
 *                 and log the configuration.
 * @return The exit code of the launcher.
 */
std::uint32_t ProbeRunRawConfig(const std::wstring& cwd, const nlohmann::json& document, const LogRun& log = {},
                                const std::wstring& config_file_name = L"config.json",
                                std::uint32_t* launcher_pid = nullptr, const std::vector<std::wstring>& args = {});

/**
 * @brief One startup file the launcher started during one run.
 *
 * The report is what the probe process of the startup file answered, so it
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

    /** Command line of the process, in UTF-8. */
    std::string command_line;

    /** Entries of the environment of the process, one `name=value` text each. */
    std::vector<std::string> environment;

    /** Current directory the process reported, in UTF-8. */
    std::string cwd;

    /**
     * @brief Outcome of one operation the process ran with a relative name.
     */
    struct RelativeOperation
    {
        /** Error code of the operation, zero on success. */
        std::uint32_t code = 0;

        /** Content the operation read, empty for the other kinds. */
        std::string data;
    };

    /** Outcomes of the operations of the task data, in the order of the list. */
    std::vector<RelativeOperation> relative_ops;
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

    /**
     * @brief Time the launcher of the run worked, in milliseconds.
     *
     * The time is what a case measures when it pins how fast a launcher answers
     * a request: the launcher of a second instance leaves as soon as the master
     * instance answered, so its lifetime is the latency of that answer and not
     * the time the programs of the request need to start and report.
     */
    std::uint32_t launcher_ms = 0;
};

/**
 * @brief One launcher run whose programs report, kept alive until the case ends
 *        it.
 *
 * The handle is the way a case holds a run while it works with the sandbox of
 * that run: the programs of the run answer the report of the case and then stay
 * alive, and the case ends them with the exit request of the probe, which is
 * what makes the launcher return. A case which ends the run as soon as its work
 * is done therefore needs no budget of its own: the lifetime of the run is the
 * one the case gives it, and no stage of the case waits for a fixed time.
 *
 * The handle owns the run, so its destructor ends the run as well: a case which
 * returns early leaves no probe and no launcher behind. A case which ends the
 * run itself calls `Exit()` and reads the exit code with `Wait()`.
 */
class StartupRunHandle
{
public:
    /**
     * @brief Run a launcher and return its exit code.
     *
     * The runner is the part of a run a case describes itself: the key of the
     * run is what the programs of the run report to, and the process id of the
     * launcher is what the reports of those programs carry.
     *
     * @param[in] key Key of the run, which the arguments of the programs carry.
     * @param[out] launcher_pid Process id of the launcher, may be null.
     * @return The exit code of the launcher.
     */
    typedef std::function<std::uint32_t(const std::string& key, std::uint32_t* launcher_pid)> LauncherRunner;

    /**
     * @brief Start a run whose launcher the case describes itself.
     *
     * The call returns as soon as the launcher was started, so the case decides
     * when the programs of the run report and when the run ends.
     *
     * @param[in] cwd Working directory of the case, which is also the directory
     *                the launcher resolves its sandbox layout against.
     * @param[in] launcher Runner of the launcher, which must not be empty.
     */
    StartupRunHandle(const std::wstring& cwd, LauncherRunner launcher);

    /**
     * @brief Start the run of a configuration, whose startup files report.
     *
     * Every startup file is started as the 64 bit probe process of the case,
     * like `ProbeStartupRun()` does, and a configuration which describes no
     * startup file starts the probe once.
     *
     * @param[in] cwd The current working directory.
     * @param[in] launcher_config The launcher configuration.
     * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
     *                    the launcher start the auto start files.
     * @param[in] startup_screen Whether the run shows the startup screen.
     * @param[in] log The log options of the run, see LogRun.
     * @param[in] use_configured_executable Whether the run starts the executable
     *                    the configuration spells instead of the probe.
     * @param[in] launcher_path Program to run instead of the launcher of the
     *                    test configuration, empty to run the configured one.
     * @param[in] working_directory Directory the launcher works in, empty to
     *                    inherit the directory of the test process.
     * @param[in] probe_data Task data of the probe processes, see
     *                    `ProtocolStartupStarted::Req`.
     */
    StartupRunHandle(const std::wstring& cwd, const LauncherConfig& launcher_config, const std::string& trigger = "",
                     bool startup_screen = false, const LogRun& log = {}, bool use_configured_executable = false,
                     const std::wstring&   launcher_path = std::wstring(),
                     const std::wstring&   working_directory = std::wstring(),
                     const nlohmann::json& probe_data = nlohmann::json::object());

    /**
     * @brief Start the run of a launcher program whose configuration is
     *        embedded in its image.
     *
     * The program is started without `--X-AppBox-ConfigFile`, so the run reads
     * the configuration resource of its own executable, which is the way a
     * packed archive runs. The probe arguments are passed as the remaining
     * command line of the launcher, which appends them to the arguments of every
     * startup file.
     *
     * @param[in] cwd The current working directory.
     * @param[in] launcher_path Path of the launcher program to run.
     * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
     *                    the launcher start the auto start files.
     * @param[in] marker Marker passed to the probe processes, empty to omit it.
     * @param[in] log The log options of the run, see LogRun.
     */
    StartupRunHandle(const std::wstring& cwd, const std::wstring& launcher_path, const std::string& trigger,
                     const std::string& marker, const LogRun& log = {});

    ~StartupRunHandle();

    StartupRunHandle(const StartupRunHandle&) = delete;
    StartupRunHandle& operator=(const StartupRunHandle&) = delete;

    /**
     * @brief Wait until the programs of the run connected.
     *
     * A connection is what proves that the run works: the probe of a program
     * sends its bind request before it answers anything, so a case which waits
     * for the connection of a program can address that program at once.
     *
     * @param[in] quiet_ms Time without a new connection after which the set of
     *                     programs is taken as complete.
     * @param[in] timeout_ms Budget of the wait.
     * @return true when at least one program connected.
     */
    bool WaitForSessions(std::uint32_t quiet_ms = kProbeBindQuietMs, std::uint32_t timeout_ms = kProbeBindTimeoutMs);

    /**
     * @brief Ask every program of the run for its report.
     *
     * The call fills the reports of the run, see `Run()`. A program which
     * cannot answer is reported in the log of the case and skipped, like
     * `ProbeRun::CallAll()` does.
     *
     * @return The reports of the run.
     * @throws std::runtime_error No program of the run connected.
     */
    const StartupRun& Collect();

    /**
     * @brief Ask every program of the run for its report without recording it.
     *
     * The answer of a program proves that the program still works, which is what
     * a case pins when it checks that a request was served while the program of
     * the run was alive: the case ends the run itself, so a program which still
     * answers cannot have left on its own. The reports of the run are the ones
     * the case collected before, so the answers of the call are counted instead
     * of stored.
     *
     * @return The number of programs which answered, zero when none did.
     */
    std::size_t Ping();

    /**
     * @brief The reports of the run and the exit code of its launcher.
     * @return The reports of the run, the exit code only after `Wait()`.
     */
    const StartupRun& Run() const;

    /**
     * @brief Ask every program of the run to leave.
     *
     * The request is the exit request of the probe, so the launcher of the run
     * returns as soon as its last program left. The call is idempotent and
     * never throws, like `ProbeRun::ExitAll()`.
     *
     * @param[in] code Exit code of the programs of the run.
     */
    void Exit(std::int32_t code = 0);

    /**
     * @brief Wait for the end of the launcher run.
     * @return The exit code of the launcher.
     * @throws std::runtime_error The launcher did not end, or it failed.
     */
    std::uint32_t Wait();

private:
    StartupRun                run_;        /* Reports of the run and its exit code */
    std::unique_ptr<ProbeRun> probe_run_;  /* Run which owns the probes of the case */
    nlohmann::json            probe_data_; /* Task data of the reports */
};

/**
 * @brief Run the launcher of a configuration and report the startup files it
 *        started.
 *
 * Every startup file of the configuration is started as the 64 bit probe
 * process of the run, so every started file reports the marker of its own
 * arguments. The case asks every probe for its report after the launcher
 * started them, and ends the probes afterwards, which is what lets the launcher
 * return.
 *
 * The executable of a startup file is replaced by the 64 bit probe, like
 * `Probe` does: the paths of a packaged configuration point into the sandbox
 * view, which does not exist on the test machine.
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
 * @param[in] working_directory Directory the launcher itself works in, empty to
 *                    inherit the directory of the test process. The directory
 *                    of the run is what the programs of the run inherit, so a
 *                    case which checks the rewriting of a directory names a
 *                    directory of a layer here.
 * @param[in] probe_data Task data of the probe processes, see
 *                    `ProtocolStartupStarted::Req`.
 * @return The reports of the started startup files and the exit code of the
 *         launcher.
 */
StartupRun ProbeStartupRun(const std::wstring& cwd, const LauncherConfig& launcher_config, const std::string& trigger,
                           bool startup_screen = false, const LogRun& log = {}, bool use_configured_executable = false,
                           const std::wstring&   launcher_path = std::wstring(),
                           const std::wstring&   working_directory = std::wstring(),
                           const nlohmann::json& probe_data = nlohmann::json::object());

/**
 * @brief Run a second launcher which asks the master instance for programs.
 *
 * The call is the client side of a start request: it starts the launcher of a
 * configuration document which names the sandbox and nothing else, so the
 * launcher hands its request over instead of preparing a sandbox of its own.
 * The programs the master instance starts for the request are probe processes
 * which report to the case, so the case observes what the request really
 * started.
 *
 * @param[in] cwd The current working directory.
 * @param[in] document Configuration document of the second instance.
 * @param[in] trigger Trigger of the request, empty to ask for the auto start
 *                    files.
 * @param[in] marker Marker of the probe processes of the request, empty to
 *                   omit it.
 * @param[in] exit_code Exit code the probe processes of the request report,
 *                      zero for a successful run.
 * @param[in] log The log options of the run, see LogRun.
 * @param[out] run Reports of the programs the master instance started for the
 *                 request, and the exit code of the second launcher.
 * @param[in] working_directory Directory the second launcher works in, empty to
 *                 inherit the directory of the test process. The directory of
 *                 the client is the one the request asks the master instance
 *                 for, so a case which checks the working directory of a
 *                 request names a directory of a layer here.
 * @param[in] probe_data Task data of the programs the request starts, see
 *                 `ProtocolStartupStarted::Req`.
 * @param[in] launcher_path Program of the second instance, empty to run the
 *                 launcher of the test configuration. A case which checks the
 *                 sandbox of a self-extracting program runs that program, so the
 *                 client runs the very image the master instance runs.
 * @param[in] config_file_name Name of the file the configuration of the second
 *                 instance is written into. Two second instances of one case
 *                 which run at the same time name a file of their own, because
 *                 both of them write it into the directory of the sandbox.
 * @return The exit code of the second launcher.
 */
std::uint32_t ProbeRunSecondary(const std::wstring& cwd, const nlohmann::json& document, const std::string& trigger,
                                const std::string& marker, std::uint32_t exit_code, const LogRun& log, StartupRun& run,
                                const std::wstring&   working_directory = std::wstring(),
                                const nlohmann::json& probe_data = nlohmann::json::object(),
                                const std::wstring&   launcher_path = std::wstring(),
                                const std::wstring&   config_file_name = std::wstring());

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
 * configuration. A probe which reports is asked for its report and ended at
 * once, because a probe which kept serving would keep the run of the launcher
 * alive.
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
