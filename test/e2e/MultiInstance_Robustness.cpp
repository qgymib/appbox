#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/MultiInstancePipe.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestSandboxId.hpp"
#include "SandboxId.hpp"
#include "WString.hpp"
#include "launcher/msg/LauncherStart.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Text the log of a master instance carries once its own program runs. */
constexpr const char* kApplicationStarted = "the initial application of the sandbox was started";

/** Budget of a case which waits for a stage of a run, in milliseconds. */
constexpr int kStageTimeoutMs = 60000;

/** Budget of a case which waits for the answer of the control pipe, in milliseconds. */
constexpr std::uint32_t kAnswerTimeoutMs = 30000;

/** Time the initial program of a master instance stays alive, in milliseconds. */
constexpr std::uint32_t kRunHoldMs = 12000;

/**
 * @brief Build the start request of one client of the case.
 * @param[in] id Identifier of the sandbox.
 * @param[in] root Host root of the sandbox.
 * @return The request.
 */
appbox::MsgLauncherStart::Req RequestOf(const std::string& id, const std::wstring& root)
{
    appbox::MsgLauncherStart::Req request;
    request.version = appbox::kLauncherStartVersion;
    request.sandbox_id = id;
    request.sandbox_root = appbox::WideToUTF8(root);
    request.selection = "auto";
    return request;
}

} // namespace

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. A client writes requests which cannot be read: a payload which is no JSON,
 *    a payload which is no object, a request without a method, an unknown
 *    method, a request which lacks a field and a request whose field is above
 *    the limit of the protocol.
 *
 * Expected:
 * 1. Every malformed payload is answered with the error of the JSON-RPC layer
 *    (`-32700`, `-32600` or `-32601`) and every request which reaches the
 *    launcher is refused with the reason of the field (`malformed_request`,
 *    `payload_too_large`).
 * 2. The pipe keeps serving the sandbox: a second launcher of the same archive
 *    hands a request over afterwards and its program reports.
 */
TEST_F(E2E_MultiInstance, MalformedRequests)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master = ProbeStartupRun(GetCWDString(), config, "", false, LogRun{}, false, std::wstring(), kRunHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    std::vector<int>         error_codes;
    std::vector<std::string> refusal_codes;

    if (running)
    {
        RawControlPipe pipe(appbox::SandboxPipeName(id));
        ASSERT_TRUE(pipe.IsOpen()) << pipe.LastError();

        nlohmann::json answer;

        /* A payload which is no JSON at all. */
        ASSERT_TRUE(pipe.SendPayload("{not json")) << pipe.LastError();
        ASSERT_TRUE(pipe.ReadResponse(answer, kAnswerTimeoutMs)) << pipe.LastError();
        error_codes.push_back(RpcErrorCode(answer));

        /* A payload which is JSON but no request. */
        ASSERT_TRUE(pipe.SendPayload("[]")) << pipe.LastError();
        ASSERT_TRUE(pipe.ReadResponse(answer, kAnswerTimeoutMs)) << pipe.LastError();
        error_codes.push_back(RpcErrorCode(answer));

        /* A request which names no method. */
        ASSERT_TRUE(pipe.SendPayload(R"({"jsonrpc":"2.0","id":3})")) << pipe.LastError();
        ASSERT_TRUE(pipe.ReadResponse(answer, kAnswerTimeoutMs)) << pipe.LastError();
        error_codes.push_back(RpcErrorCode(answer));

        /* A method the launcher does not register. */
        ASSERT_TRUE(pipe.Call(4, "NoSuchMethod", nlohmann::json::object(), answer, kAnswerTimeoutMs))
            << pipe.LastError();
        error_codes.push_back(RpcErrorCode(answer));

        /* A request of the launcher which lacks the identifier of the sandbox. */
        auto request = RequestOf(id, cwd.wstring());
        request.sandbox_id.clear();
        ASSERT_TRUE(pipe.Call(5, appbox::MsgLauncherStart::Method, nlohmann::json(request), answer, kAnswerTimeoutMs))
            << pipe.LastError();
        refusal_codes.push_back(answer["result"].value("error_code", std::string()));

        /* A request whose selection is above the limit of the protocol. */
        request = RequestOf(id, cwd.wstring());
        request.selection = std::string(appbox::launcher_start_limits::kMaxSelectionSize + 1, 'a');
        ASSERT_TRUE(pipe.Call(6, appbox::MsgLauncherStart::Method, nlohmann::json(request), answer, kAnswerTimeoutMs))
            << pipe.LastError();
        refusal_codes.push_back(answer["result"].value("error_code", std::string()));
    }

    /*
     * The service is still usable: a launcher of the archive hands a request
     * over and the program of the request reports.
     */
    StartupRun    request_run;
    std::uint32_t secondary_exit = 1;
    if (running)
    {
        secondary_exit =
            ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", "request", 0, LogRun{}, request_run);
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";

    ASSERT_EQ(error_codes.size(), static_cast<std::size_t>(4)) << error_codes.size();
    EXPECT_EQ(error_codes[0], -32700);
    EXPECT_EQ(error_codes[1], -32600);
    EXPECT_EQ(error_codes[2], -32600);
    EXPECT_EQ(error_codes[3], -32601);

    ASSERT_EQ(refusal_codes.size(), static_cast<std::size_t>(2)) << refusal_codes.size();
    EXPECT_EQ(refusal_codes[0], appbox::launcher_start_error::kMalformedRequest);
    EXPECT_EQ(refusal_codes[1], appbox::launcher_start_error::kPayloadTooLarge);

    EXPECT_EQ(secondary_exit, 0u) << "the control pipe stopped serving the sandbox";
    EXPECT_EQ(request_run.reports.size(), static_cast<std::size_t>(1)) << request_run.reports.size();
    EXPECT_EQ(master.exit_code, 0u);
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. Three second launchers of the same archive send a request at the same time,
 *    each with a marker of its own, while two clients of the case read the
 *    environment state of the sandbox over the same pipe.
 *
 * Expected:
 * 1. Every request is answered: each second launcher leaves with zero and the
 *    program of its own request reports, so no request is lost to another one.
 * 2. The environment state is written for every client which asks for it, so the
 *    two methods do not block each other on the one pipe.
 * 3. The master instance keeps its own program and leaves with zero.
 */
TEST_F(E2E_MultiInstance, ConcurrentRequests)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto config = MultiInstanceConfig(cwd, id, "app");

    StartupRun  master;
    std::thread master_thread([&]() {
        master = ProbeStartupRun(GetCWDString(), config, "", false, LogRun{}, false, std::wstring(), kRunHoldMs);
    });

    const auto running = WaitForRunLog(cwd, kApplicationStarted, kStageTimeoutMs);

    constexpr std::size_t kRequestCount = 3;
    constexpr std::size_t kEnvironmentCount = 2;

    std::vector<StartupRun>    request_runs(kRequestCount);
    std::vector<std::uint32_t> request_exits(kRequestCount, 1);
    std::vector<bool>          environment_answered(kEnvironmentCount, false);
    std::vector<std::uint32_t> environment_errors(kEnvironmentCount, 0);

    if (running)
    {
        std::vector<std::thread> workers;

        for (std::size_t index = 0; index < kRequestCount; ++index)
        {
            workers.emplace_back([&, index]() {
                const auto marker = "request" + std::to_string(index);
                const auto file_name = L"config.secondary." + std::to_wstring(index) + L".json";

                request_exits[index] = ProbeRunSecondary(cwd.wstring(), MultiInstanceRequestDocument(id), "", marker, 0,
                                                         LogRun{}, request_runs[index], std::wstring(),
                                                         nlohmann::json::object(), std::wstring(), file_name);
            });
        }

        for (std::size_t index = 0; index < kEnvironmentCount; ++index)
        {
            workers.emplace_back([&, index]() {
                RawControlPipe pipe(appbox::SandboxPipeName(id));
                if (!pipe.IsOpen())
                {
                    environment_errors[index] = pipe.LastError();
                    return;
                }

                nlohmann::json state;
                state["state"] = "{\"case\":" + std::to_string(index) + "}";

                nlohmann::json answer;
                environment_answered[index] = pipe.Call(static_cast<std::uint64_t>(100 + index), "Environment", state,
                                                        answer, kAnswerTimeoutMs) &&
                                              answer.contains("result");

                if (!environment_answered[index])
                {
                    environment_errors[index] = pipe.LastError();
                }
            });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }
    }

    master_thread.join();

    ASSERT_TRUE(running) << "the master instance never started its application";

    for (std::size_t index = 0; index < kRequestCount; ++index)
    {
        EXPECT_EQ(request_exits[index], 0u) << "request " << index << " was not answered";
        EXPECT_EQ(request_runs[index].reports.size(), static_cast<std::size_t>(1))
            << "request " << index << " started " << request_runs[index].reports.size() << " program(s)";
    }

    for (std::size_t index = 0; index < kEnvironmentCount; ++index)
    {
        EXPECT_TRUE(environment_answered[index])
            << "the environment state " << index << " was not answered: " << environment_errors[index];
    }

    EXPECT_EQ(master.exit_code, 0u);
    EXPECT_EQ(master.reports.size(), static_cast<std::size_t>(1)) << master.reports.size();
}
