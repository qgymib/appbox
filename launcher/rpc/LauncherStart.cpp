/*
 * <winsock2.h> has to precede <windows.h>, which the headers below pull in
 * (<Shlobj.h>, the project headers): it defines _WINSOCKAPI_, so <windows.h>
 * skips the winsock 1.1 header, which cannot be included next to the winsock 2
 * header the RPC server reaches through asio.
 */
#include <winsock2.h>
#include <spdlog/spdlog.h>
#include <string>
#include "WString.hpp"
#include "utils/HostPath.hpp"
#include "msg/LauncherStart.hpp"
#include "control/ControlPlane.hpp"
#include "Launcher.hpp"
#include "__init__.hpp"

/*
 * The handler of the start request of a second launcher instance.
 *
 * The call runs on the IO thread of the pipe server, which serves every method
 * of the launcher, so it does what only the request itself needs: it validates
 * the fields, authenticates the client, checks that the request belongs to this
 * sandbox and queues it. The processes of a request are created by the control
 * thread, which also answers the request, because creating a process takes long
 * enough to hold up the environment messages of the sandboxed processes on the
 * same pipe.
 */
APPBOX_LAUNCHER_RPC_DEFINE(MsgLauncherStart, id, param)
{
    auto control = LauncherApp().control;
    if (!control)
    {
        /*
         * The control layer exists before the pipe server starts, so a request
         * which arrives here without one cannot be answered: the session is
         * dropped and the client reports the failure of its call.
         */
        SPDLOG_ERROR("the launcher has no control layer, the start request is dropped");
        return;
    }

    /*
     * The fields are validated before the client is examined: the check is a
     * pure function which costs nothing, and a request which cannot be read is
     * reported as malformed instead of being reported as unauthorized, which
     * keeps the two reasons of a refusal apart for every client.
     */
    std::string error_code;
    std::string error_message;
    if (!appbox::ValidateLauncherStartRequest(param, error_code, error_message))
    {
        control->Refuse(id, error_code, error_message);
        return;
    }

    /*
     * The client is authenticated before anything of the request is kept: the
     * identifier and the root of the sandbox are readable by every process the
     * sandbox runs, so the process at the other end of the pipe is the only
     * boundary which decides whether a program may be started at all.
     */
    if (!control->Authenticate(id, error_code, error_message))
    {
        control->Refuse(id, error_code, error_message);
        return;
    }

    if (param.sandbox_id != LauncherApp().launcher_config.sandbox_id)
    {
        control->Refuse(id, appbox::launcher_start_error::kSandboxMismatch,
                        "the request belongs to the sandbox '" + param.sandbox_id + "'");
        return;
    }

    /*
     * Two copies of one archive carry the same identifier and different roots,
     * so the root tells whether the client is a copy of this sandbox or another
     * one. The comparison is the one of the host: the path is expanded, the
     * trailing separators are dropped and the case is not significant.
     */
    if (!appbox::SameHostPath(appbox::UTF8ToWide(param.sandbox_root), control->SandboxRoot()))
    {
        control->Refuse(id, appbox::launcher_start_error::kSandboxMismatch,
                        "the sandbox of the request lives in another directory");
        return;
    }

    appbox::LauncherStartRequest request;
    request.request = param;
    request.rpc_id = id;

    switch (control->Enqueue(std::move(request)))
    {
    case appbox::EnqueueResult::Queued:
        return;

    case appbox::EnqueueResult::ShuttingDown:
        /*
         * The run ends, but the sandbox may be started again at once: the client
         * races for the control pipe again instead of reporting a failure.
         */
        control->ReplyStatus(id, appbox::launcher_start_status::kShuttingDown,
                             appbox::launcher_start_error::kShuttingDown, "the sandbox ends its run", 0);
        return;

    case appbox::EnqueueResult::Busy:
    default:
        control->Refuse(id, appbox::launcher_start_error::kBusy, "the request queue of the sandbox is full");
        return;
    }
}
