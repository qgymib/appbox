/*
 * <winsock2.h> has to precede <windows.h>, which the headers below pull in
 * (<Shlobj.h>, the project headers): it defines _WINSOCKAPI_, so <windows.h>
 * skips the winsock 1.1 header, which cannot be included next to the winsock 2
 * header the RPC server reaches through asio.
 */
#include <winsock2.h>
#include <spdlog/spdlog.h>
#include <string>
#include "msg/RunState.hpp"
#include "control/ControlPlane.hpp"
#include "Launcher.hpp"
#include "__init__.hpp"

/*
 * The handler of the state query of a run.
 *
 * The call runs on the IO thread of the pipe server, so it neither waits for the
 * control thread nor creates anything: it copies the facts the run recorded and
 * the accounting of the control layer, which are readable while the processes of
 * the run still work. A refused query answers with the reason and no state, so a
 * client never reads the zero of every member as a fact of the run.
 */
APPBOX_LAUNCHER_RPC_DEFINE(MsgRunState, id, param)
{
    auto control = LauncherApp().control;
    if (!control)
    {
        /*
         * The control layer exists before the pipe server starts, so a query
         * which arrives here without one cannot be answered: the session is
         * dropped and the client reports the failure of its call.
         */
        SPDLOG_ERROR("the launcher has no control layer, the state query is dropped");
        return;
    }

    appbox::MsgRunState::Rsp response;

    if (param.version != appbox::kRunStateVersion)
    {
        response.error_code = appbox::run_state_error::kVersionUnsupported;
        response.error_message = "the version " + std::to_string(param.version) + " of the query is not supported";
        session->SendResponse(id, nlohmann::json(response));
        return;
    }

    /*
     * The query starts nothing, so the client is only compared with the user and
     * the integrity level of the run, see `AuthenticateQuery()`.
     */
    std::string error_code;
    std::string error_message;
    if (!control->AuthenticateQuery(session, id, error_code, error_message))
    {
        response.error_code = error_code;
        response.error_message = error_message;
        session->SendResponse(id, nlohmann::json(response));
        return;
    }

    /* The facts of the run itself and the accounting of its processes. */
    LauncherApp().FillRunState(response);
    control->FillRunState(response);

    session->SendResponse(id, nlohmann::json(response));
}
