#ifndef APPBOX_LAUNCHER_CONTROL_CONTROLPLANE_HPP
#define APPBOX_LAUNCHER_CONTROL_CONTROLPLANE_HPP

#include "RemoteAccepter.hpp"
#include "Config.hpp"
#include "control/PeerIdentity.hpp"
#include "control/ShutdownRetry.hpp"
#include "msg/LauncherStart.hpp"
#include "msg/RunState.hpp"
#include "sandbox/Config.hpp"
#include "utils/ProcessJobSet.hpp"
#include "utils/ViewPathResolver.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Timeout of one wait for the processes of the run, in milliseconds.
 *
 * The control thread is woken up by a request of another launcher, so the
 * timeout only bounds the wait of a run which received no request: a wakeup
 * which was lost still ends the wait, and the control thread handles its
 * requests instead of hanging forever.
 */
constexpr DWORD kControlWaitTimeoutMs = 1000;

/** @brief Budget of a second instance to reach the control pipe of a sandbox. */
constexpr std::uint32_t kSecondaryConnectTimeoutMs = 5000;

/** @brief Budget of a second instance to wait for the answer of a request. */
constexpr std::uint32_t kSecondaryStartTimeoutMs = 120000;

/** @brief Number of requests which may wait for the control thread. */
constexpr std::size_t kMaxQueuedRequests = 64;

/**
 * @brief One process the launcher of a run starts.
 *
 * The target describes what the launcher creates and not what the configuration
 * says: the image is the host path of the layer which holds the program, while
 * the command line names the path of the view.
 */
struct LaunchTarget
{
    std::string               name;                 /*!< Name of the target, for the log. */
    std::wstring              exe_path;             /*!< Executable to run, as the configuration spells it. */
    std::wstring              image_path;           /*!< Host path of the image which is created. */
    std::vector<std::wstring> args;                 /*!< Arguments of the executable. */
    bool                      hide_console = false; /*!< Start the process without a console window. */
    std::string               error;                /*!< Reason the target cannot be started at all. */
};

/**
 * @brief One request of another launcher which waits for the control thread.
 *
 * The request keeps the session it arrived on, because the control thread
 * answers it: a session is the only place which knows the request id of its
 * peer, and the connection of a request may end while the request still waits.
 */
struct LauncherStartRequest
{
    appbox::RemoteSession::Ptr session;        /*!< Session which sent the request. */
    MsgLauncherStart::Req      request;        /*!< Request of the second instance. */
    std::uint64_t              rpc_id = 0;     /*!< Request id of the session which has to be answered. */
    std::uint64_t              request_id = 0; /*!< Id the run assigned, which names the jobs of the request. */
};

/**
 * @brief Select the startup files of one selection.
 *
 * The selection of a request names the startup files of the configuration of
 * the master instance, so a request never names a program of its own.
 *
 * @param[in] config Configuration of the master instance.
 * @param[in] selection Selection of the request.
 * @param[out] selected The startup files of the selection, in configuration
 *                      order; empty for the selection of the shell.
 * @param[out] error_code Stable identifier of the reason, see
 *                        `launcher_start_error`; empty on success.
 * @param[out] error_message Human readable description, empty on success.
 * @return true when the selection names at least one startup file, or when it
 *         selects the shell, which the caller builds itself.
 */
bool SelectStartupFiles(const LauncherConfig& config, const LauncherStartSelection& selection,
                        std::vector<const LauncherStartup*>& selected, std::string& error_code,
                        std::string& error_message);

/**
 * @brief Build the processes of the selected startup files.
 *
 * Every startup file is turned into the process which runs it (see
 * `appbox::BuildStartupInvocation()`), the arguments of the request are appended
 * to the arguments of the file, and the image of the program is resolved
 * through the view of the run. A target which cannot be built carries the
 * reason in its `error` member instead of failing the call, so a caller decides
 * whether a partly built run is refused.
 *
 * @param[in] startups Startup files to start, in the order they are started in.
 * @param[in] extra_args Arguments which every startup file receives after its
 *                       own ones.
 * @param[in] hide_console True to start the programs without a console window.
 * @param[in] resolver View of the run.
 * @param[out] targets The processes to start, in the order they are started in.
 */
void BuildLaunchTargets(const std::vector<const LauncherStartup*>& startups, const std::vector<std::string>& extra_args,
                        bool hide_console, const ViewPathResolver& resolver, std::vector<LaunchTarget>& targets);

/**
 * @brief Build the process of one shell.
 *
 * The shell of a run is a program of the host and not a startup file, so its
 * command line is built by the caller: the initial run of the launcher hands
 * over the command of its own command line, and a request hands over the
 * arguments it carries.
 *
 * @param[in] shell_path Path of the shell of the host.
 * @param[in] args Arguments of the shell.
 * @param[in] resolver View of the run.
 * @param[out] target The process to start.
 * @param[out] error Reason the image cannot be resolved, empty on success.
 * @return true when the image of the shell was resolved.
 */
bool BuildShellTarget(const std::wstring& shell_path, const std::vector<std::wstring>& args,
                      const ViewPathResolver& resolver, LaunchTarget& target, std::string& error);

/**
 * @brief Build the start request of this process.
 *
 * The request describes what this launcher was asked to run: the selection of
 * its own command line, the arguments which were not consumed by the launcher,
 * the environment it sees and the directory it works in. The identifier and the
 * root of the sandbox are taken from the configuration, so a request never
 * describes another sandbox than the one the launcher belongs to.
 *
 * @param[in] config Configuration of this launcher.
 * @param[in] sandbox_root Host directory which carries the sandbox.
 * @param[in] selection Selection of the request, see
 *                      `appbox::MakeLauncherStartSelection()`.
 * @param[in] args Arguments of the request, in UTF-8.
 * @return The request.
 */
MsgLauncherStart::Req BuildLauncherStartRequest(const LauncherConfig& config, const std::wstring& sandbox_root,
                                                const std::string& selection, const std::vector<std::string>& args);

/**
 * @brief Map the reason of a refused request to the exit code of the client.
 * @param[in] error_code Stable identifier of the reason.
 * @param[in] win32_error Win32 code of a failed creation, zero when the reason
 *                        is none of a creation.
 * @return The exit code of the second instance.
 */
DWORD LauncherStartExitCode(const std::string& error_code, int win32_error);

/**
 * @brief Outcome of the handshake of a second instance.
 *
 * A master instance which is closing its sandbox answers no request any more,
 * but the sandbox itself may be started again at once: the run which receives
 * the answer therefore either leaves with a code or races for the control pipe
 * a second time, and the two outcomes have to be told apart by the caller which
 * owns the race.
 */
struct SecondaryHandover
{
    bool  retry = false; /*!< True when the run has to race for the pipe again. */
    DWORD exit_code = 0; /*!< Exit code of the run when no further race is asked for. */
};

/**
 * @brief Hand the request of this process to the master instance of its sandbox.
 *
 * The call connects to the control pipe of the sandbox, verifies that the server
 * is the master instance of the sandbox of the request, sends the request and
 * waits for the answer. A master instance which answers that the programs were
 * started ends the run of this process with zero: the second instance does not
 * wait for the programs it asked for.
 *
 * A server which is closing the sandbox answers `shutting_down` or closes the
 * pipe, which is reported as a request to race again: the caller waits for the
 * interval of the shutdown race and competes for the pipe once more, so a
 * request is never lost to the end of the run which served the sandbox.
 *
 * @param[in] pipe Name of the control pipe of the sandbox.
 * @param[in] request Request to send.
 * @return The outcome of the handshake.
 */
SecondaryHandover RunSecondaryInstance(const std::string& pipe, const MsgLauncherStart::Req& request);

/**
 * @brief Outcome of queueing a request which passed the validation.
 */
enum class EnqueueResult
{
    Queued,      /* The request waits for the control thread. */
    Busy,        /* The queue is full, the client is asked to try again later. */
    ShuttingDown /* The run is ending and serves no request any more. */
};

/**
 * @brief The control layer of the master instance of a sandbox.
 *
 * The layer owns the pipe server of the run, the queue of the requests which
 * arrived over it, the processes of the run and the view which resolves the
 * programs a request starts. The RPC thread only validates and queues a
 * request, while the control thread builds the processes and answers the
 * request, so a request never blocks the environment messages of the sandboxed
 * processes on the same pipe.
 *
 * The processes of the run belong to the layer as well, because a request is
 * handled while the programs of the run still work: a queued request wakes the
 * wait of the control thread up, and the processes a request starts join the
 * same set, so the run ends only after every process of every instance exited.
 *
 * The view of the run is built once, on the first use: reading the isolation
 * files of every layer is expensive and a request must not pay for it.
 */
class ControlPlane
{
public:
    typedef std::shared_ptr<ControlPlane> Ptr;

    /**
     * @brief Build the control layer of one run.
     * @param[in] accepter Accepter which serves the control pipe of the sandbox.
     * @param[in] sandbox_root Host directory which carries the sandbox.
     */
    ControlPlane(appbox::RemoteAccepter::Ptr accepter, const std::wstring& sandbox_root);
    ~ControlPlane();

    ControlPlane(const ControlPlane&) = delete;
    ControlPlane& operator=(const ControlPlane&) = delete;

    /**
     * @brief Get the accepter of the run.
     * @return The accepter, which owns the control pipe of the sandbox.
     */
    const appbox::RemoteAccepter::Ptr& Server() const;

    /**
     * @brief Get the host directory which carries the sandbox.
     * @return The root the control layer was built for.
     */
    const std::wstring& SandboxRoot() const;

    /**
     * @brief Take the injection data of the run and build the view of it.
     *
     * The call has to run after the runtime of the run exists and before the
     * first request is processed.
     *
     * @param[in] inject_data Injection data of the run.
     */
    void BindInjectData(const appbox::SandboxConfig& inject_data);

    /**
     * @brief Get the view of the run.
     * @return The resolver, which is built on the first call.
     */
    const appbox::ViewPathResolver& Resolver() const;

    /**
     * @brief Authenticate the client which sent one request.
     *
     * The identifier and the root of a sandbox are readable by every process the
     * sandbox runs, so they are no authentication: the call compares the process
     * at the other end of the session with the run itself and accepts it only
     * when it runs the launcher of this sandbox under the same user and at the
     * same integrity level (see `appbox::control::AuthorizePeer()`).
     *
     * The call runs on the RPC thread and reads the identity of the client,
     * which costs a constant number of queries; it neither creates a process nor
     * blocks on the control thread.
     *
     * @param[in] session Session which sent the request.
     * @param[in] rpc_id Id of the request of the session which sent it.
     * @param[out] error_code Stable identifier of the reason, see
     *                        `launcher_start_error`; empty when the client is
     *                        authorized.
     * @param[out] error_message Human readable description, empty when the
     *                           client is authorized.
     * @return true when the client may send a start request.
     */
    bool Authenticate(const appbox::RemoteSession::Ptr& session, std::uint64_t rpc_id, std::string& error_code,
                      std::string& error_message);

    /**
     * @brief Authenticate the client which asks for the state of the run.
     *
     * The state query starts nothing, so the check is the one of a read-only
     * client of the run: it runs as the user of the master instance at the
     * integrity level of the master instance (see
     * `appbox::control::AuthorizeQuery()`). The image of the client is not part
     * of the check, unlike the one of a start request.
     *
     * @param[in] session Session which sent the query.
     * @param[in] rpc_id Id of the request of the session which sent it.
     * @param[out] error_code Stable identifier of the reason, see
     *                        `run_state_error`; empty when the client is
     *                        authorized.
     * @param[out] error_message Human readable description, empty when the
     *                           client is authorized.
     * @return true when the client may read the state of the run.
     */
    bool AuthenticateQuery(const appbox::RemoteSession::Ptr& session, std::uint64_t rpc_id, std::string& error_code,
                           std::string& error_message);

    /**
     * @brief Fill the control layer members of a state answer.
     *
     * The call reports the requests the run queued and handled, the processes a
     * rollback terminated and every program of the run with its origin, its
     * request and its exit code. The facts of the run itself belong to the
     * launcher, which fills them (see `AppBoxLauncher::FillRunState()`).
     *
     * @param[out] state Answer to fill, whose run facts are left untouched.
     */
    void FillRunState(appbox::MsgRunState::Rsp& state);

    /**
     * @brief Queue a request which passed the validation and the authentication.
     *
     * The call runs on the RPC thread and returns at once: the request is
     * queued, the id it is known by is assigned and the wait of the control
     * thread is woken up, so the request is handled while the programs of the
     * run still work.
     *
     * @param[in] request Request to queue.
     * @return Whether the request was queued, and why it was not.
     */
    EnqueueResult Enqueue(LauncherStartRequest request);

    /**
     * @brief Stop serving the sandbox and answer the requests which still wait.
     *
     * The run calls the method once it decided to end: new requests are answered
     * with `shutting_down` instead of being queued, the requests which already
     * wait are answered the same way, and the pipe server is stopped, which
     * refuses new connections, serves the answers which are in flight and then
     * releases the pipe. The call is idempotent, so every path which leaves the
     * run may call it.
     */
    void Shutdown();

    /**
     * @brief Report whether the accept loop of the pipe server died.
     * @return true when the server serves no connection any more.
     */
    bool AcceptFailed() const;

    /**
     * @brief Take the failure of the accept loop of the pipe server.
     *
     * The call runs on the IO thread of the server, which may neither block nor
     * throw: it keeps the failure and wakes the control thread up, and the
     * control loop reports the error and ends the run. The pipe is stopped by
     * the thread which owns the run, because stopping it from the IO thread
     * would wait for that very thread.
     *
     * @param[in] kind Kind of the failure.
     * @param[in] error_code Win32 code of the failure.
     */
    void OnAcceptFailure(appbox::AcceptFailureKind kind, DWORD error_code);

    /**
     * @brief Get the number of requests which wait for the control thread.
     * @return The number of queued requests.
     */
    std::size_t QueuedRequests() const;

    /**
     * @brief Get the processes of the run.
     *
     * The set belongs to the layer, so the run starts its own programs into it
     * and the requests add theirs to the same accounting. The caller which
     * starts the programs of the run names them as the initial origin, which is
     * what keeps them the only ones which decide the exit code of the run.
     *
     * @return The processes of the run.
     */
    appbox::ProcessJobSet& Jobs();

    /**
     * @brief Serve the requests of the other launchers until the run ends.
     *
     * The call handles every queued request and then waits for the processes of
     * the run. A request which arrives wakes the wait up, so it is handled
     * while the programs of the run still work; the call returns only when no
     * request waits and every process of the run exited.
     *
     * @return The result of the wait which ended the run: `AllExited` for a run
     *         which is complete, `Error` for a wait which failed or for a pipe
     *         server whose accept loop died.
     */
    appbox::WaitResult WaitForPrograms();

    /**
     * @brief Process every queued request.
     *
     * Every request is answered over the pipe server of the run: with the
     * process ids of the programs it started, or with the reason it was
     * refused. The call runs on the control thread, because it creates the
     * processes of the requests.
     */
    void DrainQueue();

    /**
     * @brief Answer a request with the reason it is refused.
     * @param[in] session Session which sent the request.
     * @param[in] rpc_id Request id of the session to answer.
     * @param[in] error_code Stable identifier of the reason.
     * @param[in] error_message Human readable description.
     * @param[in] win32_error Win32 code of a failed creation, zero otherwise.
     */
    void Refuse(const appbox::RemoteSession::Ptr& session, std::uint64_t rpc_id, const std::string& error_code,
                const std::string& error_message, DWORD win32_error = 0);

    /**
     * @brief Answer a request with a status and the reason of it.
     * @param[in] session Session which sent the request.
     * @param[in] rpc_id Request id of the session to answer.
     * @param[in] status Status of the answer, see `launcher_start_status`.
     * @param[in] error_code Stable identifier of the reason, empty for a status
     *                       which carries no reason.
     * @param[in] error_message Human readable description of the reason.
     * @param[in] win32_error Win32 code of a failed creation, zero otherwise.
     */
    void ReplyStatus(const appbox::RemoteSession::Ptr& session, std::uint64_t rpc_id, const std::string& status,
                     const std::string& error_code, const std::string& error_message, DWORD win32_error);

private:
    /**
     * @brief Build the processes of one request.
     * @param[in] request Request to build.
     * @param[out] targets The processes to start.
     * @param[out] error_code Stable identifier of the reason, empty on success.
     * @param[out] error_message Human readable description, empty on success.
     * @return true when every process of the request can be created.
     */
    bool BuildRequestTargets(const MsgLauncherStart::Req& request, std::vector<LaunchTarget>& targets,
                             std::string& error_code, std::string& error_message);

    /**
     * @brief Resolve the current directory of one request into the host.
     *
     * The directory a request carries is a path of the view when the client
     * runs inside the sandbox and a path of the host otherwise: the call
     * resolves it with the view of the run, which answers the directory of the
     * layer which holds it, and the path of the host for a directory the view
     * does not carry. The launcher creates the process outside of the sandbox,
     * so only the resolved path is usable as the working directory of the
     * process, which the system checks on the host.
     *
     * @param[in] request Request to read the directory of.
     * @param[out] directory Host directory of the request, empty when the
     *                       request asks for the directory of the run.
     * @param[out] error_message Reason the directory cannot be resolved, empty
     *                           on success.
     * @return true when the programs of the request may be created in it.
     */
    bool ResolveRequestDirectory(const MsgLauncherStart::Req& request, std::wstring& directory,
                                 std::string& error_message);

    /**
     * @brief Create the processes of one request and answer it.
     *
     * A request whose programs cannot all be created is rolled back: the
     * processes it already created are terminated and the request is refused,
     * so the run never carries half a request.
     *
     * @param[in] request Request to process.
     */
    void ProcessRequest(LauncherStartRequest& request);

    /**
     * @brief Answer a request.
     * @param[in] session Session which sent the request.
     * @param[in] rpc_id Request id of the session to answer.
     * @param[in] response Answer to send.
     */
    void Reply(const appbox::RemoteSession::Ptr& session, std::uint64_t rpc_id, const MsgLauncherStart::Rsp& response);

    struct Data;
    Data* data_;
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_CONTROL_CONTROLPLANE_HPP
