#include "RemoteConnector.hpp"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>
#include "Bind.hpp"
#include "Exit.hpp"
#include "__init__.hpp"

#define PROBE_COMMAND "probe"

/** Budget of the connection to the test executable, in milliseconds. */
constexpr std::uint32_t kConnectTimeoutMs = 30000;

/** Budget of the bind request which tells the test executable who owns this process. */
constexpr std::uint32_t kBindTimeoutMs = 30000;

/** Budget of the flush of the answer of the exit request, in milliseconds. */
constexpr std::uint32_t kFlushTimeoutMs = 5000;

/** Delay between two polls of the exit request, in milliseconds. */
constexpr std::uint32_t kPollDelayMs = 10;

struct ProbeCtx
{
    typedef std::shared_ptr<ProbeCtx> Ptr;

    /*
     * The connector owns the IO thread the session runs on, so it is kept beside
     * the session and released after it.
     */
    appbox::RemoteConnector::Ptr connector; /* Connector of the control pipe */
    appbox::RemoteSession::Ptr   session;   /* Session of the connection */
    std::string                  probe_key; /* Key of the handle which started this process */
};

/**
 * @brief Probe command context.
 */
static ProbeCtx::Ptr s_probe_ctx;

/**
 * @brief Marker of the startup file which started this process.
 */
static std::string s_startup_marker;

/**
 * @brief The handlers of the probes of this executable, by method.
 */
static std::map<std::string, appbox::test::ProbeHandler> s_handlers;

/**
 * @brief Bind one handler of a probe to the session it is registered on.
 *
 * A session answers its own requests, so the handler needs the session it
 * serves. The handler is stored inside the session and therefore holds it
 * weakly: a strong reference would keep the connection alive for ever.
 *
 * @param[in] session Session the handler is registered on.
 * @param[in] handler Handler of the probe.
 * @return The handler to register on the session.
 */
static appbox::RemoteSession::MethodHandler BindHandler(const appbox::RemoteSession::Ptr& session,
                                                        const appbox::test::ProbeHandler& handler)
{
    std::weak_ptr<appbox::RemoteSession> weak_session = session;

    return [weak_session, handler](uint64_t id, const nlohmann::json& param) {
        auto bound = weak_session.lock();
        if (!bound)
        {
            return;
        }

        try
        {
            bound->SendResponse(id, handler(param));
        }
        catch (const nlohmann::json::exception& e)
        {
            SPDLOG_ERROR("the parameters of a probe cannot be read: {}", e.what());
            bound->SendResponse(id, tl::unexpected<appbox::RemoteError>(appbox::RemoteError{
                                        appbox::test::kInvalidParamsCode, "Invalid params", nlohmann::json() }));
        }
    };
}

/**
 * @brief Store the marker a case names for this process.
 *
 * The option exists beside `--startup_marker` because the launcher appends the
 * arguments of a request to the arguments of the startup file, which already
 * carries the marker of the file: an option which is given twice is refused by
 * the command line parser, so a request names the marker of its own processes
 * with an option of its own, which overrides the marker of the file.
 *
 * @param[in] marker Marker of the process.
 */
static void SetProbeMarker(const std::string& marker)
{
    s_startup_marker = marker;
}

/**
 * @brief Connect this process to the test executable which started it.
 * @param[in] url Path of the control pipe of the case.
 * @throws std::runtime_error The pipe cannot be opened.
 */
static void ParseProbePipe(const std::wstring& url)
{
    SPDLOG_INFO("connect the probe to '{}'", CLI::narrow(url));

    s_probe_ctx->connector = appbox::RemoteConnector::Create(CLI::narrow(url));
    s_probe_ctx->session = s_probe_ctx->connector->Connect(kConnectTimeoutMs);
    if (!s_probe_ctx->session)
    {
        SPDLOG_ERROR("Failed to connect the probe to the pipe");
        throw std::runtime_error("Failed to connect the probe to the pipe");
    }
}

/**
 * @brief Serve the requests of the test executable until it asks this process
 *        to leave.
 *
 * The process reports which handle it belongs to first, so a run which starts
 * several probes lets the case tell them apart. It then answers the requests of
 * the case until the exit request arrives; the answer of that request is
 * flushed before the process leaves, because a process which left earlier would
 * drop it and the caller would wait for its whole budget.
 *
 * A pipe which ends without an exit request means that the test executable is
 * gone: no request can reach this process any more, so it leaves as well. A
 * probe which kept waiting would keep the launcher of its run alive, and that
 * launcher holds the state of the sandbox, which the artifact of a failed run
 * has to carry.
 *
 * @throws CLI::RuntimeError The exit request named a non zero code, the process
 *                           could not bind itself to its handle, or the pipe of
 *                           the case ended without an exit request.
 */
static void Serve()
{
    const auto& session = s_probe_ctx->session;

    /*
     * The report of the end of the connection is what ends a process the case
     * did not ask to leave, so it is registered before the process answers
     * anything: a case which is gone before the bind is answered is told apart
     * by the failure of the bind below.
     */
    auto connection_closed = std::make_shared<std::atomic_bool>(false);
    session->SetClosedHandler([connection_closed]() { *connection_closed = true; });

    for (const auto& probe : s_handlers)
    {
        session->RegisterMethod(probe.first, BindHandler(session, probe.second));
    }

    appbox::test::ProtocolBind::Req bind;
    bind.key = s_probe_ctx->probe_key;

    const auto bound = session->Call<appbox::test::ProtocolBind>(bind, kBindTimeoutMs);
    if (!bound.has_value())
    {
        SPDLOG_ERROR("ProtocolBind failed: {}", bound.error().message);
        throw CLI::RuntimeError(EXIT_FAILURE);
    }

    SPDLOG_INFO("the probe serves the handle '{}'", s_probe_ctx->probe_key);

    while (!appbox::test::ExitRequested() && !connection_closed->load())
    {
        ::Sleep(kPollDelayMs);
    }

    if (!appbox::test::ExitRequested())
    {
        /*
         * The pipe is gone, so there is nothing to flush and no code to report:
         * the process leaves the way a program of a launcher which ended does.
         */
        SPDLOG_ERROR("the pipe of the case ended, the probe leaves");
        throw CLI::RuntimeError(EXIT_FAILURE);
    }

    /*
     * The exit request is answered by the handler of the IO thread, which may
     * still be running: the wait for the incoming requests covers it, the wait
     * for the pending writes covers the answer itself.
     */
    session->WaitForIncomingResponses(kFlushTimeoutMs);
    session->WaitForPendingWrites(kFlushTimeoutMs);

    const auto code = appbox::test::ExitCode();
    SPDLOG_INFO("the probe leaves with code {}", code);

    if (code != 0)
    {
        throw CLI::RuntimeError(code);
    }

    throw CLI::Success();
}

void appbox::test::RegisterProbeMethod(const std::string& method, ProbeHandler handler)
{
    s_handlers[method] = std::move(handler);
}

void appbox::test::ProbeInit(CLI::App& app)
{
    s_probe_ctx = std::make_shared<ProbeCtx>();
    RegisterProbes();

    app.add_option("--startup_marker", s_startup_marker, "Marker of the startup file which started this process");
    app.add_option_function<std::string>("--probe_marker", SetProbeMarker,
                                         "Marker of this process, which overrides the marker of the startup file");

    auto cmd = app.add_subcommand(PROBE_COMMAND);
    cmd->add_option_function<std::wstring>("--probe_pipe", ParseProbePipe)->required();
    cmd->add_option("--probe_key", s_probe_ctx->probe_key)->required();
    cmd->final_callback(Serve);
}

const std::string& appbox::test::StartupMarker()
{
    return s_startup_marker;
}
