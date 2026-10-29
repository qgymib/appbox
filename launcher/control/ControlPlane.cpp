/*
 * <winsock2.h> has to precede <windows.h>, which the headers below pull in
 * (<Shlobj.h>, the project headers): it defines _WINSOCKAPI_, so <windows.h>
 * skips the winsock 1.1 header, which cannot be included next to the winsock 2
 * header the RPC server reaches through asio.
 */
#include <winsock2.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "RemoteClient.hpp"
#include "KnownFolder.hpp"
#include "WString.hpp"
#include "utils/HostPath.hpp"
#include "utils/Shell.hpp"
#include "control/ControlPlane.hpp"
#include "Launcher.hpp"

namespace
{

/**
 * @brief Compare two trigger names ignoring case.
 *
 * A trigger is written by a user, so the case of the name a request carries is
 * not significant: the configuration of the master instance decides which
 * startup file a trigger names.
 *
 * @param[in] a Left operand.
 * @param[in] b Right operand.
 * @return true when both names are equal ignoring case.
 */
bool EqualsIgnoreCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < a.size(); ++index)
    {
        const auto left = std::tolower(static_cast<unsigned char>(a[index]));
        const auto right = std::tolower(static_cast<unsigned char>(b[index]));
        if (left != right)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Get the command line arguments of one startup file.
 * @param[in] startup Startup file to convert.
 * @return The arguments in wide characters.
 */
std::vector<std::wstring> BuildCmdArg(const appbox::LauncherStartup& startup)
{
    std::vector<std::wstring> args;
    for (const auto& arg : startup.arguments)
    {
        args.push_back(appbox::UTF8ToWide(arg));
    }

    return args;
}

/**
 * @brief Build the environment block of a request.
 *
 * The block is the form `CreateProcessW` reads with the Unicode environment
 * flag: every entry is a `name=value` text which ends with a null character,
 * and the block ends with a second null character.
 *
 * @param[in] environment Environment of the request.
 * @param[out] block The block to fill.
 */
void BuildEnvironmentBlock(const std::vector<appbox::MsgLauncherStart::EnvironmentEntry>& environment,
                           std::vector<wchar_t>&                                          block)
{
    block.clear();

    for (const auto& entry : environment)
    {
        const auto line = appbox::UTF8ToWide(entry.key) + L"=" + appbox::UTF8ToWide(entry.value);
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(L'\0');
    }

    /* The block of a process ends with a second null character. */
    block.push_back(L'\0');
}

/**
 * @brief Collect the environment of this process as key and value pairs.
 *
 * The block `GetEnvironmentStringsW()` reports carries the current directory of
 * every drive as an entry whose name starts with `=`, which is no variable: a
 * caller which splits the block at the first `=` would turn such an entry into
 * a variable with an empty name. The call skips those entries, exactly like the
 * request of a second instance is defined to.
 *
 * @return The variables of this process, in the order of the block.
 */
std::vector<appbox::MsgLauncherStart::EnvironmentEntry> CollectEnvironment()
{
    std::vector<appbox::MsgLauncherStart::EnvironmentEntry> entries;

    wchar_t* block = GetEnvironmentStringsW();
    if (block == nullptr)
    {
        SPDLOG_WARN("the environment of the launcher cannot be read: {}", GetLastError());
        return entries;
    }

    for (const wchar_t* cursor = block; *cursor != L'\0'; cursor += std::wstring(cursor).size() + 1)
    {
        if (*cursor == L'=')
        {
            continue;
        }

        const std::wstring entry(cursor);
        const auto         separator = entry.find(L'=');
        if (separator == std::wstring::npos)
        {
            continue;
        }

        appbox::MsgLauncherStart::EnvironmentEntry variable;
        variable.key = appbox::WideToUTF8(entry.substr(0, separator));
        variable.value = appbox::WideToUTF8(entry.substr(separator + 1));
        entries.push_back(std::move(variable));
    }

    FreeEnvironmentStringsW(block);
    return entries;
}

/**
 * @brief Get the current directory of this process.
 * @return The directory in UTF-8, empty when it cannot be read.
 */
std::string CurrentDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0)
        {
            SPDLOG_WARN("the current directory of the launcher cannot be read: {}", GetLastError());
            return {};
        }

        if (length < buffer.size())
        {
            return appbox::WideToUTF8(std::wstring(buffer.data(), length));
        }

        buffer.resize(length + 1);
    }
}

/**
 * @brief Verify that the server of a control pipe is the master instance of a sandbox.
 *
 * The client of a pipe learns the process at the other end of the connection
 * from the system, and that process is compared with this run: it has to run the
 * image of this run as the same user and at the same integrity level. A process
 * which occupied the name of the control pipe of a sandbox is therefore refused
 * before the request of this run is sent to it.
 *
 * @param[in] client Client which is connected to the pipe.
 * @param[out] error_message Reason the server is not trusted, empty on success.
 * @return true when the server may receive the request.
 */
bool VerifyMasterInstance(appbox::RemoteClient& client, std::string& error_message)
{
    error_message.clear();

    appbox::control::ProcessIdentity own;
    if (!appbox::control::QueryOwnIdentity(own))
    {
        error_message = "the identity of this launcher cannot be read";
        return false;
    }

    const DWORD server_pid = client.ServerProcessId();
    if (server_pid == 0)
    {
        error_message = "the process at the other end of the control pipe is unknown";
        return false;
    }

    appbox::control::ProcessIdentity server;
    if (!appbox::control::QueryProcessIdentity(server_pid, server))
    {
        error_message = "the identity of the server of the control pipe cannot be read";
        return false;
    }

    const auto verdict = appbox::control::AuthorizeServer(server, own, error_message);
    return verdict == appbox::control::PeerVerdict::Authorized;
}

} // namespace

bool appbox::SelectStartupFiles(const LauncherConfig& config, const LauncherStartSelection& selection,
                                std::vector<const LauncherStartup*>& selected, std::string& error_code,
                                std::string& error_message)
{
    selected.clear();
    error_code.clear();
    error_message.clear();

    if (selection.kind == LauncherStartSelectionKind::Trigger)
    {
        for (const auto& startup : config.startups)
        {
            if (EqualsIgnoreCase(startup.trigger, selection.trigger))
            {
                selected.push_back(&startup);
                return true;
            }
        }

        error_code = launcher_start_error::kTriggerNotFound;
        error_message = "no startup file uses the trigger '" + selection.trigger + "'";
        return false;
    }

    if (selection.kind == LauncherStartSelectionKind::Shell)
    {
        /* The shell is no startup file: the caller builds the process itself. */
        return true;
    }

    for (const auto& startup : config.startups)
    {
        if (startup.auto_start)
        {
            selected.push_back(&startup);
        }
    }

    if (selected.empty())
    {
        error_code = launcher_start_error::kNoTarget;
        error_message = "no startup file is marked for auto start";
        return false;
    }

    return true;
}

void appbox::BuildLaunchTargets(const std::vector<const LauncherStartup*>& startups,
                                const std::vector<std::string>& extra_args, bool hide_console,
                                const ViewPathResolver& resolver, std::vector<LaunchTarget>& targets)
{
    for (const auto* startup : startups)
    {
        LaunchTarget target;
        target.name = startup->trigger;
        target.hide_console = hide_console;

        /*
         * The arguments of the request follow the arguments of the startup
         * file, so a request never changes what the configuration describes.
         */
        auto args = BuildCmdArg(*startup);
        for (const auto& extra : extra_args)
        {
            args.push_back(appbox::UTF8ToWide(extra));
        }

        const auto path = appbox::ExpandKnownFolder(appbox::UTF8ToWide(startup->executable));
        appbox::BuildStartupInvocation(path, args, target.exe_path, target.args, target.error);
        if (target.error.empty())
        {
            resolver.Resolve(target.exe_path, target.image_path, target.error);
        }

        targets.push_back(std::move(target));
    }
}

bool appbox::BuildShellTarget(const std::wstring& shell_path, const std::vector<std::wstring>& args,
                              const ViewPathResolver& resolver, LaunchTarget& target, std::string& error)
{
    target.name = "shell";
    target.exe_path = shell_path;
    target.args = args;
    /* The console window of the shell is always shown: it is used interactively. */
    target.hide_console = false;
    target.error.clear();

    error.clear();
    resolver.Resolve(target.exe_path, target.image_path, error);
    if (!error.empty())
    {
        target.error = error;
        return false;
    }

    return true;
}

appbox::MsgLauncherStart::Req appbox::BuildLauncherStartRequest(const LauncherConfig&           config,
                                                                const std::wstring&             sandbox_root,
                                                                const std::string&              selection,
                                                                const std::vector<std::string>& args)
{
    MsgLauncherStart::Req request;
    request.version = kLauncherStartVersion;
    request.sandbox_id = config.sandbox_id;
    request.sandbox_root = appbox::WideToUTF8(NormalizeHostPath(sandbox_root));
    request.selection = selection;
    request.args = args;
    request.environment = CollectEnvironment();
    /*
     * The environment of the client is the environment of the host: the
     * injection module of the sandbox leaves every launcher alone, so a launcher
     * never runs inside a sandbox and never hands a composed environment over.
     * The sandbox of the created process composes it from the request instead.
     */
    request.cwd = CurrentDirectory();
    return request;
}

DWORD appbox::LauncherStartExitCode(const std::string& error_code, int win32_error)
{
    /*
     * Every reason which reports a client, a server or a sandbox the run does
     * not trust leaves with the code of a refused access: the second instance
     * has no program to report a failure for.
     */
    if (error_code == launcher_start_error::kSandboxMismatch || error_code == launcher_start_error::kUnauthorized ||
        error_code == launcher_start_error::kIntegrityMismatch || error_code == launcher_start_error::kServerMismatch)
    {
        return ERROR_ACCESS_DENIED;
    }

    if (error_code == launcher_start_error::kVersionUnsupported ||
        error_code == launcher_start_error::kMalformedRequest || error_code == launcher_start_error::kPayloadTooLarge)
    {
        return ERROR_INVALID_DATA;
    }

    if (error_code == launcher_start_error::kBusy)
    {
        return ERROR_BUSY;
    }

    if (error_code == launcher_start_error::kNoTarget || error_code == launcher_start_error::kTriggerNotFound ||
        error_code == launcher_start_error::kShellNotFound || error_code == launcher_start_error::kImageNotFound)
    {
        return ERROR_FILE_NOT_FOUND;
    }

    if (error_code == launcher_start_error::kCwdNotFound)
    {
        return ERROR_DIRECTORY;
    }

    /*
     * A request which is refused because the run ends is raced again by the
     * client; the code below is what it reports when its budget is used up.
     */
    if (error_code == launcher_start_error::kShuttingDown)
    {
        return ERROR_TIMEOUT;
    }

    if (error_code == launcher_start_error::kStartFailed)
    {
        return win32_error > 0 ? static_cast<DWORD>(win32_error) : ERROR_GEN_FAILURE;
    }

    return ERROR_GEN_FAILURE;
}

appbox::SecondaryHandover appbox::RunSecondaryInstance(const std::string& pipe, const MsgLauncherStart::Req& request)
{
    SecondaryHandover handover;

    auto client = appbox::RemoteClient::Create(pipe);
    if (!client->Start(kSecondaryConnectTimeoutMs))
    {
        /*
         * The pipe was named by the race which this run lost, so it existed a
         * moment ago: a pipe which cannot be reached any more belongs to a run
         * which is closing, and the sandbox may be started again at once.
         */
        SPDLOG_INFO("the master instance of the sandbox cannot be reached over '{}'", pipe);
        handover.retry = true;
        handover.exit_code = ERROR_TIMEOUT;
        return handover;
    }

    /*
     * The server is verified before the request is sent: a process which
     * occupied the name of the control pipe is answered nothing, so a request
     * of this run never reaches a server which is no master instance of the
     * sandbox.
     */
    std::string server_error;
    if (!VerifyMasterInstance(*client, server_error))
    {
        SPDLOG_ERROR("the server of the control pipe is not the master instance of the sandbox: {}", server_error);
        handover.exit_code = ERROR_ACCESS_DENIED;
        return handover;
    }

    SPDLOG_INFO("hand the start request over to the master instance of the sandbox");

    const auto result = client->Call(MsgLauncherStart::Method, nlohmann::json(request), kSecondaryStartTimeoutMs);
    if (!result.has_value())
    {
        if (result.error().code == static_cast<int>(ERROR_TIMEOUT))
        {
            /* The master instance is alive but does not answer, which is no race to retry. */
            SPDLOG_ERROR("the master instance did not answer the start request: {}", result.error().message);
            handover.exit_code = ERROR_TIMEOUT;
            return handover;
        }

        /*
         * A call which cannot be carried out any more means that the pipe was
         * closed between the connection and the request, which is the shutdown
         * of the sandbox: the request is raced again.
         */
        SPDLOG_INFO("the control pipe of the sandbox was closed before the request was answered: {}",
                    result.error().message);
        handover.retry = true;
        handover.exit_code = ERROR_TIMEOUT;
        return handover;
    }

    MsgLauncherStart::Rsp response;
    try
    {
        response = result.value().get<MsgLauncherStart::Rsp>();
    }
    catch (const nlohmann::json::exception& e)
    {
        SPDLOG_ERROR("the answer of the master instance cannot be read: {}", e.what());
        handover.exit_code = ERROR_INVALID_DATA;
        return handover;
    }

    /*
     * The answer names the sandbox of the server which sent it: a server of
     * another sandbox is refused instead of being trusted.
     */
    if (response.sandbox_id != request.sandbox_id)
    {
        SPDLOG_ERROR("the answer names the sandbox '{}' instead of '{}'", response.sandbox_id, request.sandbox_id);
        handover.exit_code = ERROR_ACCESS_DENIED;
        return handover;
    }

    if (response.status == launcher_start_status::kStarted)
    {
        for (const auto pid : response.pids)
        {
            SPDLOG_INFO("the master instance started the process {}", pid);
        }

        SPDLOG_INFO("the sandbox runs the application, the second instance leaves");
        return handover;
    }

    if (response.status == launcher_start_status::kShuttingDown)
    {
        /* The sandbox ends its run, but the request is not lost: it races again. */
        SPDLOG_INFO("the master instance is closing the sandbox, the request is raced again");
        handover.retry = true;
        handover.exit_code = ERROR_TIMEOUT;
        return handover;
    }

    SPDLOG_ERROR("the master instance refused the start request: {} ({})", response.error_code, response.error_message);
    handover.exit_code = LauncherStartExitCode(response.error_code, response.win32_error);
    return handover;
}

struct appbox::ControlPlane::Data
{
    Data(appbox::RemoteServer::Ptr pipe_server, std::wstring root)
        : server(std::move(pipe_server)), sandbox_root(std::move(root))
    {
    }

    appbox::RemoteServer::Ptr                 server;       /* Pipe server which serves the sandbox. */
    std::wstring                              sandbox_root; /* Host directory which carries the sandbox. */
    std::deque<LauncherStartRequest>          queue;        /* Requests which wait for the control thread. */
    mutable std::mutex                        mutex;        /* Guards the queue, which the IO thread fills. */
    appbox::ProcessJobSet                     jobs;         /* Processes of the run, of the requests included. */
    std::atomic<std::uint64_t>                next_request_id{ 1 };    /* Id of the next request of a launcher. */
    std::unique_ptr<appbox::ViewPathResolver> resolver;                /* View of the run, built once. */
    appbox::SandboxConfig                     inject_data;             /* Injection data of the run. */
    bool                                      has_inject_data = false; /* True once the runtime exists. */
    appbox::control::ProcessIdentity          own_identity;            /* Identity of the run, read once. */
    std::atomic<bool>                         shutting_down{ false };  /* True once the run ends. */
    std::atomic<bool>                         accept_failed{ false };  /* True once the pipe serves no connection. */
    std::atomic<DWORD>                        accept_error{ 0 };       /* Code of the failure of the accept loop. */
};

appbox::ControlPlane::ControlPlane(appbox::RemoteServer::Ptr server, const std::wstring& sandbox_root)
    : data_(new Data(std::move(server), sandbox_root))
{
    /*
     * The identity of the run is read once: every request of another launcher is
     * compared with it, and reading it again for every request would query the
     * same token over and over. A run which cannot read its own identity refuses
     * every request, which is the safe direction of the check.
     */
    if (!appbox::control::QueryOwnIdentity(data_->own_identity))
    {
        SPDLOG_WARN("the identity of this launcher cannot be read, every start request of the sandbox is refused");
    }
}

appbox::ControlPlane::~ControlPlane()
{
    /*
     * The server is stopped before the queue is released: a handler which is in
     * flight answers over it, and the stop waits for the handlers of the pipe.
     */
    if (data_->server)
    {
        data_->server->Stop();
    }

    delete data_;
}

const appbox::RemoteServer::Ptr& appbox::ControlPlane::Server() const
{
    return data_->server;
}

const std::wstring& appbox::ControlPlane::SandboxRoot() const
{
    return data_->sandbox_root;
}

void appbox::ControlPlane::BindInjectData(const appbox::SandboxConfig& inject_data)
{
    data_->inject_data = inject_data;
    data_->has_inject_data = true;

    if (!data_->resolver)
    {
        data_->resolver = std::make_unique<appbox::ViewPathResolver>(data_->inject_data);
    }
}

const appbox::ViewPathResolver& appbox::ControlPlane::Resolver() const
{
    if (!data_->resolver)
    {
        /*
         * A request cannot arrive before the runtime of the run exists, because
         * the queue is only processed after it. A call which reaches this point
         * anyway resolves the view of an empty configuration, which is the view
         * of a run without layers, instead of dereferencing nothing.
         */
        SPDLOG_ERROR("the view of the sandbox is asked for before the run was prepared");
        data_->resolver = std::make_unique<appbox::ViewPathResolver>(data_->inject_data);
    }

    return *data_->resolver;
}

bool appbox::ControlPlane::Authenticate(std::uint64_t rpc_id, std::string& error_code, std::string& error_message)
{
    error_code.clear();
    error_message.clear();

    /*
     * The identity of a client is read before anything of the request is done:
     * the identifier and the root of a sandbox are readable by every process the
     * sandbox runs, so only the process which sent the request tells whether the
     * request may start a program at all.
     */
    const DWORD peer_pid = data_->server != nullptr ? data_->server->PeerProcessIdOfRequest(rpc_id) : 0;
    if (peer_pid == 0)
    {
        error_code = launcher_start_error::kUnauthorized;
        error_message = "the process at the other end of the control pipe is unknown";
        SPDLOG_WARN("the start request is refused: {}", error_message);
        return false;
    }

    appbox::control::ProcessIdentity peer;
    if (!appbox::control::QueryProcessIdentity(peer_pid, peer))
    {
        error_code = launcher_start_error::kUnauthorized;
        error_message = "the identity of the process " + std::to_string(peer_pid) + " cannot be read";
        SPDLOG_WARN("the start request is refused: {}", error_message);
        return false;
    }

    switch (appbox::control::AuthorizePeer(peer, data_->own_identity, error_message))
    {
    case appbox::control::PeerVerdict::Authorized:
        SPDLOG_DEBUG("the start request of the process {} is authorized", peer_pid);
        return true;

    case appbox::control::PeerVerdict::IntegrityMismatch:
        error_code = launcher_start_error::kIntegrityMismatch;
        break;

    case appbox::control::PeerVerdict::Unauthorized:
    default:
        error_code = launcher_start_error::kUnauthorized;
        break;
    }

    SPDLOG_WARN("the start request of the process {} is refused: {} ({})", peer_pid, error_code, error_message);
    return false;
}

appbox::EnqueueResult appbox::ControlPlane::Enqueue(LauncherStartRequest request)
{
    /*
     * The request is named by an id of the run and not by the id of its RPC
     * session: the rollback of a request terminates the jobs of that id, and
     * the ids of two sessions may name the same number.
     */
    request.request_id = data_->next_request_id.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(data_->mutex);

        /*
         * A run which decided to end serves no request any more: the client is
         * told that the sandbox is closing, which is the answer it races again
         * for instead of reporting a failure of its own.
         */
        if (data_->shutting_down.load())
        {
            return EnqueueResult::ShuttingDown;
        }

        if (data_->queue.size() >= kMaxQueuedRequests)
        {
            return EnqueueResult::Busy;
        }

        data_->queue.push_back(std::move(request));
    }

    /*
     * The wait of the control thread is woken up after the request is queued,
     * so the thread which is woken up always finds the request in the queue.
     */
    data_->jobs.Wake();

    SPDLOG_INFO("the start request of the sandbox is queued, {} request(s) wait", QueuedRequests());
    return EnqueueResult::Queued;
}

void appbox::ControlPlane::Shutdown()
{
    /*
     * The order is the one of the shutdown of the sandbox: the run stops taking
     * requests, answers the requests which already wait, and only then closes
     * the pipe, which serves the answers in flight before it is released. A
     * client which arrives in between is answered `shutting_down`, so no request
     * is lost to the end of the run.
     */
    if (data_->shutting_down.exchange(true))
    {
        return;
    }

    std::deque<LauncherStartRequest> pending;
    {
        std::lock_guard<std::mutex> lock(data_->mutex);
        pending.swap(data_->queue);
    }

    for (const auto& request : pending)
    {
        ReplyStatus(request.rpc_id, launcher_start_status::kShuttingDown, launcher_start_error::kShuttingDown,
                    "the sandbox ends its run", 0);
    }

    if (!pending.empty())
    {
        SPDLOG_INFO("%zu start request(s) are answered with the end of the run", pending.size());
    }

    if (data_->server)
    {
        data_->server->Stop();
    }
}

bool appbox::ControlPlane::AcceptFailed() const
{
    return data_->accept_failed.load();
}

void appbox::ControlPlane::OnAcceptFailure(appbox::AcceptFailureKind kind, DWORD error_code)
{
    /*
     * The callback runs on the IO thread of the pipe server, which may neither
     * block nor throw: the failure is kept and the control thread is woken up,
     * and that thread stops the pipe, because stopping it here would wait for
     * this very thread.
     */
    data_->accept_error.store(error_code);
    data_->accept_failed.store(true);
    data_->jobs.Wake();

    SPDLOG_ERROR("the control pipe of the sandbox serves no connection any more: kind {}, error {}",
                 static_cast<int>(kind), error_code);
}

std::size_t appbox::ControlPlane::QueuedRequests() const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    return data_->queue.size();
}

appbox::ProcessJobSet& appbox::ControlPlane::Jobs()
{
    return data_->jobs;
}

appbox::WaitResult appbox::ControlPlane::WaitForPrograms()
{
    for (;;)
    {
        /*
         * A pipe which serves no connection any more is the end of the run: the
         * sandbox of the run cannot be reached by the launchers which ask for
         * its programs, so waiting for the programs alone would keep a sandbox
         * alive which nobody can join.
         */
        if (AcceptFailed())
        {
            const DWORD error = data_->accept_error.load();
            SPDLOG_ERROR("the control pipe of the sandbox died, the run ends: {}", error);
            return appbox::WaitResult{ appbox::WaitStatus::Error, error != 0 ? error : ERROR_GEN_FAILURE };
        }

        /*
         * The queue is drained before the wait, so a request which arrived
         * while the control thread worked is handled without waiting for the
         * next wakeup, and a wakeup is never lost between the two steps.
         */
        DrainQueue();

        const auto result = data_->jobs.WaitAll(kControlWaitTimeoutMs);
        switch (result.status)
        {
        case appbox::WaitStatus::Woken:
        case appbox::WaitStatus::TimedOut:
            /* The wakeup asks for the queued requests, the timeout asks for the same. */
            continue;

        case appbox::WaitStatus::AllExited:
            /*
             * A request which arrived after the last drain still waits for its
             * answer, so the run is only complete without a queued request.
             */
            if (QueuedRequests() == 0)
            {
                return result;
            }
            continue;

        case appbox::WaitStatus::Error:
        default:
            SPDLOG_ERROR("failed to wait for the processes of the run: {}", result.error);
            return result;
        }
    }
}

void appbox::ControlPlane::DrainQueue()
{
    for (;;)
    {
        LauncherStartRequest request;
        {
            std::lock_guard<std::mutex> lock(data_->mutex);
            if (data_->queue.empty())
            {
                return;
            }

            request = std::move(data_->queue.front());
            data_->queue.pop_front();
        }

        ProcessRequest(request);
    }
}

void appbox::ControlPlane::Refuse(std::uint64_t rpc_id, const std::string& error_code, const std::string& error_message,
                                  DWORD win32_error)
{
    SPDLOG_WARN("the start request is refused: {} ({})", error_code, error_message);
    ReplyStatus(rpc_id, launcher_start_status::kRefused, error_code, error_message, win32_error);
}

void appbox::ControlPlane::ReplyStatus(std::uint64_t rpc_id, const std::string& status, const std::string& error_code,
                                       const std::string& error_message, DWORD win32_error)
{
    MsgLauncherStart::Rsp response;
    response.status = status;
    response.sandbox_id = LauncherApp().launcher_config.sandbox_id;
    response.error_code = error_code;
    response.error_message = error_message;
    response.win32_error = static_cast<int>(win32_error);

    Reply(rpc_id, response);
}

void appbox::ControlPlane::Reply(std::uint64_t rpc_id, const MsgLauncherStart::Rsp& response)
{
    if (!data_->server)
    {
        SPDLOG_ERROR("the sandbox has no pipe server, the answer of the request is dropped");
        return;
    }

    data_->server->SendResponse(rpc_id, nlohmann::json(response));
}

bool appbox::ControlPlane::BuildRequestTargets(const MsgLauncherStart::Req& request, std::vector<LaunchTarget>& targets,
                                               std::string& error_code, std::string& error_message)
{
    targets.clear();
    error_code.clear();
    error_message.clear();

    LauncherStartSelection selection;
    if (!ParseLauncherStartSelection(request.selection, selection))
    {
        error_code = launcher_start_error::kMalformedRequest;
        error_message = "the selection '" + request.selection + "' of the request is not known";
        return false;
    }

    const auto& config = LauncherApp().launcher_config;

    if (selection.kind == LauncherStartSelectionKind::Shell)
    {
        const auto shell = ResolveShellPath();
        if (shell.empty())
        {
            error_code = launcher_start_error::kShellNotFound;
            error_message = "the machine offers no shell to run";
            return false;
        }

        LaunchTarget target;
        if (!BuildShellTarget(shell, BuildShellArguments(request.args), Resolver(), target, error_message))
        {
            error_code = launcher_start_error::kImageNotFound;
            return false;
        }

        targets.push_back(std::move(target));
        return true;
    }

    std::vector<const LauncherStartup*> startups;
    if (!SelectStartupFiles(config, selection, startups, error_code, error_message))
    {
        return false;
    }

    BuildLaunchTargets(startups, request.args, config.hide_console, Resolver(), targets);

    for (const auto& target : targets)
    {
        if (!target.error.empty())
        {
            error_code = launcher_start_error::kImageNotFound;
            error_message = target.error;
            return false;
        }
    }

    return true;
}

bool appbox::ControlPlane::ResolveRequestDirectory(const MsgLauncherStart::Req& request, std::wstring& directory,
                                                   std::string& error_message)
{
    directory.clear();
    error_message.clear();

    /*
     * An empty directory asks the created process to inherit the directory of
     * the run, which is what a client without a directory of its own asks for.
     */
    if (request.cwd.empty())
    {
        return true;
    }

    const std::wstring view_path = appbox::UTF8ToWide(request.cwd);
    if (view_path.empty())
    {
        error_message = "the current directory of the request cannot be read";
        return false;
    }

    std::string error;
    if (!Resolver().Resolve(view_path, directory, error))
    {
        error_message = "the current directory '" + request.cwd + "' of the request cannot be resolved: " + error;
        directory.clear();
        return false;
    }

    SPDLOG_DEBUG("the current directory of the request is '{}'", appbox::WideToUTF8(directory));
    return true;
}

void appbox::ControlPlane::ProcessRequest(LauncherStartRequest& request)
{
    SPDLOG_INFO("handle the start request of the sandbox: selection '{}', {} argument(s)", request.request.selection,
                request.request.args.size());

    std::vector<LaunchTarget> targets;
    std::string               error_code;
    std::string               error_message;
    if (!BuildRequestTargets(request.request, targets, error_code, error_message))
    {
        Refuse(request.rpc_id, error_code, error_message);
        return;
    }

    const auto& runtime = LauncherApp().runtime;
    if (!runtime)
    {
        Refuse(request.rpc_id, launcher_start_error::kStartFailed, "the sandbox of the run is not prepared");
        return;
    }

    /*
     * Every request creates its processes from its own configuration copy: the
     * control pipe of the sandbox stays the same, while the environment of the
     * request decides whether the sandbox composes it.
     */
    appbox::SandboxConfig inject_data = runtime->inject_data;
    inject_data.environment_is_composed = request.request.environment_is_composed;

    /*
     * The options own the environment block of the request, so the processes
     * of the request read the block of the request and not the environment of
     * the launcher, which is what a request of a launcher inside the sandbox
     * asks for.
     */
    appbox::ProcessJobOptions options;
    if (!request.request.environment.empty())
    {
        BuildEnvironmentBlock(request.request.environment, options.environment);
    }

    /*
     * The working directory is resolved before the first process of the request
     * is created: a directory the view cannot name refuses the request as a
     * whole, so no process of the request has to be rolled back.
     */
    std::wstring current_directory;
    std::string  cwd_error;
    if (!ResolveRequestDirectory(request.request, current_directory, cwd_error))
    {
        Refuse(request.rpc_id, launcher_start_error::kCwdNotFound, cwd_error);
        return;
    }

    options.current_directory = std::move(current_directory);

    MsgLauncherStart::Rsp response;
    response.status = launcher_start_status::kStarted;
    response.sandbox_id = LauncherApp().launcher_config.sandbox_id;

    auto& jobs = data_->jobs;

    for (const auto& target : targets)
    {
        /* The job of the target joins the set at this index. */
        const auto index = jobs.Size();

        const auto ret = jobs.Start(target.image_path, target.exe_path, target.args, inject_data, target.hide_console,
                                    options, appbox::JobOrigin::Request, request.request_id);
        if (ret != 0)
        {
            /*
             * A request whose programs cannot all be created is rolled back as
             * a whole: the processes it already created are terminated, so the
             * run never carries half a request. The programs of the run and the
             * programs of another request are not touched.
             */
            const auto terminated = jobs.TerminateRequest(request.request_id);
            SPDLOG_ERROR("failed to start '{}' of the request: {}, {} process(es) of the request terminated",
                         target.name, ret, terminated);
            Refuse(request.rpc_id, launcher_start_error::kStartFailed, "the program of the request cannot be created",
                   ret);
            return;
        }

        const auto* job = jobs.Job(index);
        response.pids.push_back(job != nullptr ? job->ProcessId() : 0u);
    }

    Reply(request.rpc_id, response);

    for (const auto pid : response.pids)
    {
        SPDLOG_INFO("the start request started the process {}", pid);
    }
}
