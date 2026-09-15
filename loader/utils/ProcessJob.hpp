#ifndef APPBOX_LOADER_UTILS_PROCESSJOB_HPP
#define APPBOX_LOADER_UTILS_PROCESSJOB_HPP

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
     * @param[in] exePath Path of the target executable.
     * @param[in] args Arguments of the target executable.
     * @param[in] inject_data Injection data written into the target process.
     */
    ProcessJob(const std::wstring exePath, const std::vector<std::wstring> args,
               const appbox::SandboxConfig& inject_data);
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

#endif
