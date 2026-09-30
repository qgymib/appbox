#ifndef APPBOX_SANDBOX_UTILS_CRASH_REPORT_HPP
#define APPBOX_SANDBOX_UTILS_CRASH_REPORT_HPP

#include "utils/WinAPI.h" /* Must be first include file */

namespace appbox
{

/**
 * @brief Report every fatal exception of the process in the log file of the process.
 *
 * The sandbox is injected into a process which it does not own, so a crash of
 * that process has to be explainable from the log alone: the report names the
 * exception, the registers, the faulting address and the stack, and every
 * address is resolved against the module it belongs to, so a dump of the log
 * can be symbolized offline with the modules of the build.
 *
 * The handler is a vectored exception handler and an unhandled exception
 * filter: the first one reports an exception before the runtime of the
 * application sees it, the second one reports an exception nobody handled.
 * Both return `EXCEPTION_CONTINUE_SEARCH`, so the behaviour of the process is
 * the behaviour it would have without the sandbox.
 *
 * The module table is collected while the function is called, because the
 * handler itself must not take a lock of the loader or allocate memory: it
 * only formats into a buffer of the stack and appends it to the log file.
 *
 * @note The function is called while the sandbox is attached, before the hooks
 *       are installed, so it runs against the original entry points of the
 *       process.
 */
void InstallCrashHandler();

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_CRASH_REPORT_HPP
