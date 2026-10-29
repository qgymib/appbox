#ifndef APPBOX_LAUNCHER_UTILS_PROCESSJOB_HPP
#define APPBOX_LAUNCHER_UTILS_PROCESSJOB_HPP

#include "WinAPI.h"
#include "sandbox/Config.hpp"
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Optional parameters of the process a job creates.
 *
 * The launcher inherits its own environment and its own working directory to
 * every process of a run, which is what an empty option asks for. A request of
 * another launcher of the sandbox names both, see `docs/Launcher.md`.
 */
struct ProcessJobOptions
{
    /**
     * @brief Environment block of the process, empty to inherit the environment of the launcher.
     *
     * The block is the UTF-16 form `CreateProcessW` reads, which is why a
     * creation which carries a block names it as a Unicode environment. The
     * option owns the block: a request of another launcher describes the
     * environment of the process it asks for and is released before the
     * processes of the request are created, so a block which the option only
     * pointed at would be gone by then.
     */
    std::vector<wchar_t> environment;

    /**
     * @brief Working directory of the process, empty to inherit the directory of the launcher.
     *
     * The path names a directory of the host and not of the view: the launcher
     * creates the process outside the sandbox, so the system checks the
     * directory on the host, and a directory which exists in a layer of the
     * sandbox only is not usable here.
     */
    std::wstring current_directory;
};

/**
 * @brief Get the creation flags of a process of a job.
 *
 * The process is always created suspended, so the sandbox module is injected
 * and the job is filled before the first instruction of the process runs. The
 * console of a program which is started without a window is created hidden, a
 * creation which carries an environment block names it as Unicode, and a
 * creation which names its job by an attribute carries the extended startup
 * information the attribute list lives in.
 *
 * @param[in] hide_console True to start the process without a console window.
 * @param[in] has_environment True when the creation carries an environment
 *                            block instead of inheriting the environment.
 * @param[in] job_in_creation True when the creation names the job by an
 *                            attribute, so the process joins it while it is
 *                            created.
 * @return The flags of the creation.
 */
DWORD ProcessCreationFlags(bool hide_console, bool has_environment, bool job_in_creation);

struct ProcessJob
{
    /**
     * @brief Construct a process job.
     *
     * The launcher injects the sandbox module into the process and creates it
     * itself, so the image of the program and the path its command line names
     * are two different things: the image lives in the layer of the archive
     * which holds it, while the command line names the path of the view the
     * configuration spells, exactly like a sandboxed process would spell it.
     *
     * The job reports to the completion port of the set which owns it, so one
     * wait covers every process of a run. A job which names no port is not
     * associated with one, which is what a caller without a set asks for.
     *
     * @param[in] image_path Host path of the image to create.
     * @param[in] command_line_path Path the command line names as its program.
     * @param[in] args Arguments of the program.
     * @param[in] inject_data Injection data written into the target process.
     * @param[in] hide_console True to start the target without a console
     *                         window.
     * @param[in] completion_port Completion port the job reports to, null for
     *                            a job which reports to no port.
     * @param[in] options Environment and working directory of the process, see
     *                    ProcessJobOptions.
     */
    ProcessJob(const std::wstring image_path, const std::wstring command_line_path,
               const std::vector<std::wstring> args, const appbox::SandboxConfig& inject_data, bool hide_console,
               HANDLE completion_port, const appbox::ProcessJobOptions& options = {});
    ~ProcessJob();

    /**
     * @brief Get the exit code of the process.
     *
     * The code is the one the process reported, which a caller reads after the
     * job reported that its processes exited; a job which never created a
     * process reports zero.
     *
     * @return The exit code of the process.
     */
    DWORD GetExitCode();

    /**
     * @brief Get the process id of the process the job created.
     *
     * The id is the one of the process itself, not the one of the helper the
     * sandbox module is injected through, so a caller reports the process it
     * started. The id stays readable after the process exited.
     *
     * @return The process id, zero while the job created no process.
     */
    DWORD ProcessId() const;

    /**
     * @brief Start the process.
     *
     * The process joins the job while it is created where the platform
     * supports the job attribute of a creation, and is assigned to the job
     * before it is resumed where it does not. A creation which cannot put the
     * process into the job, cannot inject the sandbox module or cannot resume
     * the process terminates the process and reports the error.
     *
     * @return 0 if success, otherwise an error code.
     */
    DWORD Start();

    /**
     * @brief Get the handle of the job object of the process.
     * @return The handle, which the job owns and which is valid until the job
     *         is destroyed.
     */
    HANDLE JobHandle() const;

    /**
     * @brief Terminate every process the job holds.
     *
     * The job keeps its processes and its accounting: the completion port of
     * the set still reports that the job became empty, which is what a wait of
     * the run counts. A caller which rolls back a request terminates the jobs
     * of that request only, see `appbox::ProcessJobSet::TerminateRequest()`.
     *
     * @param[in] exit_code Exit code the terminated processes report.
     * @return 0 when the job was terminated, otherwise the error code of the
     *         call.
     */
    DWORD Terminate(DWORD exit_code);

    struct Data;
    Data* data_;
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_PROCESSJOB_HPP
