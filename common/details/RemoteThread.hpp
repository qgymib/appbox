#ifndef APPBOX_COMMON_DETAILS_REMOTE_THREAD_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_THREAD_HPP

/*
 * Helpers shared by the RPC components of `common/`: the thread which a
 * component owns and the handles of the Win32 objects it owns. The header is
 * included by the detail headers of the components and is not meant to be
 * included on its own.
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>
#include <system_error>
#include <thread>

namespace appbox
{
namespace detail
{

/**
 * @brief Index of the first handle of a wait set.
 *
 * `WaitForMultipleObjects()` and `WaitForSingleObject()` report a signalled
 * handle as `WAIT_OBJECT_0` plus the position of the handle, and `WAIT_OBJECT_0`
 * is the null status of `<ntstatus.h>`: the macro cannot be used in a
 * translation unit which does not include that header, so the components of the
 * RPC layer compare the index with the position of their handle.
 */
constexpr DWORD kWaitObjectFirst = 0;

/**
 * @brief End the given thread, or detach it when the caller runs on it.
 *
 * A component of the RPC layer may be released by a handler which runs on the
 * very thread that component owns, so joining the thread there would wait for
 * the caller for ever. The thread function of every component holds a reference
 * to the state which owns it, so a detached thread keeps that state alive until
 * the function returns and releases the handle of the component on its own.
 *
 * @param[in,out] thread Thread to end; a thread which is not joinable is left
 *                       untouched.
 */
inline void JoinOrDetach(std::thread& thread)
{
    if (!thread.joinable())
    {
        return;
    }

    if (std::this_thread::get_id() == thread.get_id())
    {
        thread.detach();
        return;
    }

    thread.join();
}

/**
 * @brief Close a handle and replace it with `INVALID_HANDLE_VALUE`.
 *
 * The call is idempotent, so a component may release the same handle on every
 * path which ends it without tracking whether another path released it first.
 *
 * @param[in,out] handle Handle to close, set to `INVALID_HANDLE_VALUE`.
 */
inline void CloseHandleIfValid(HANDLE& handle)
{
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
    {
        ::CloseHandle(handle);
    }

    handle = INVALID_HANDLE_VALUE;
}

/**
 * @brief Describe a Win32 error code the way the system describes it.
 *
 * The text is used for the log lines of the RPC layer, which report the reason
 * a pipe or a session ended next to the numeric code.
 *
 * @param[in] error Error code, as `GetLastError()` reports it.
 * @return The system description of the code, empty for a code it does not
 *         describe.
 */
inline std::string DescribeWin32Error(DWORD error)
{
    const std::error_code code(static_cast<int>(error), std::system_category());
    return code.message();
}

} // namespace detail
} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_REMOTE_THREAD_HPP
