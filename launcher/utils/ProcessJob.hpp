#ifndef APPBOX_LAUNCHER_UTILS_PROCESSJOB_HPP
#define APPBOX_LAUNCHER_UTILS_PROCESSJOB_HPP

#include "sandbox/utils/WinAPI.h"
#include "sandbox/Config.hpp"
#include <string>
#include <vector>

namespace appbox
{

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
     * @param[in] image_path Host path of the image to create.
     * @param[in] command_line_path Path the command line names as its program.
     * @param[in] args Arguments of the program.
     * @param[in] inject_data Injection data written into the target process.
     * @param[in] hide_console True to start the target without a console
     *                         window.
     */
    ProcessJob(const std::wstring image_path, const std::wstring command_line_path,
               const std::vector<std::wstring> args, const appbox::SandboxConfig& inject_data, bool hide_console);
    ~ProcessJob();

    /**
     * @brief Get the exit code of the process.
     * @return The exit code of the process.
     */
    DWORD GetExitCode();

    /**
     * @brief Start the process group.
     * @return 0 if success, otherwise an error code.
     */
    DWORD Start();

    /**
     * @brief Wait for the process group to exit. Use #GetExitCode() to get the exit code.
     * @param[in] dwMilliseconds Timeout in milliseconds.
     * @return 0 if the process group exited, ERROR_TIMEOUT if the process group did not exit within the specified
     * timeout, or ERROR_INVALID_HANDLE if the process group handle is invalid.
     */
    DWORD Wait(DWORD dwMilliseconds);

    struct Data;
    Data* data_;
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_PROCESSJOB_HPP
