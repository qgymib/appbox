/*
 * The RPC headers pull in winsock2.h through asio, they have to come before
 * <windows.h> which would otherwise include the winsock.h version 1 header.
 */
#include "RemoteAccepter.hpp"
#include <winsock2.h>
#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "SandboxId.hpp"
#include "Test.hpp"
#include "WString.hpp"
#include "launcher/msg/LauncherStart.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/MultiInstance.hpp"
#include "utils/MultiInstancePipe.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestSandboxId.hpp"

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Budget of a case which waits for the program of a run, in milliseconds. */
constexpr std::uint32_t kStageTimeoutMs = 60000;

/** Budget of a case which waits for the answer of the control pipe, in milliseconds. */
constexpr std::uint32_t kAnswerTimeoutMs = 30000;

/** Integrity level the case gives its client, which is the low level of Windows. */
constexpr const wchar_t* kLowIntegritySid = L"S-1-16-4096";

/**
 * @brief Outcome of one attempt to run a launcher at another integrity level.
 */
struct LowIntegrityRun
{
    bool          supported = false; /*!< True when the platform ran the client. */
    std::string   detail;            /*!< Reason the attempt was not possible. */
    std::uint32_t exit_code = 0;     /*!< Exit code of the client which ran. */
};

/**
 * @brief Run a launcher as a second instance at a lower integrity level.
 *
 * The client of a control pipe has to run at the integrity level of the master
 * instance, so a case which pins the refusal needs a client of another level.
 * The call duplicates the token of the process of the case, lowers the level of
 * the copy and creates the launcher with it. A machine which does not allow the
 * process to lower its own level, or to create a process with the copy, reports
 * the attempt as unsupported instead of failing the case.
 *
 * @param[in] cwd Working directory of the client, which is the sandbox root.
 * @param[in] document Configuration document of the second instance.
 * @param[in] config_file_name Name of the file the document is written into.
 * @return The outcome of the attempt.
 */
LowIntegrityRun RunSecondaryAtLowIntegrity(const std::wstring& cwd, const nlohmann::json& document,
                                           const std::wstring& config_file_name)
{
    LowIntegrityRun run;

    const auto config_path = std::filesystem::path(cwd) / config_file_name;
    {
        std::ofstream stream(config_path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            run.detail = "the configuration of the client cannot be written";
            return run;
        }

        const auto text = document.dump(2);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &token) == FALSE)
    {
        run.detail = "OpenProcessToken: " + std::to_string(GetLastError());
        return run;
    }

    HANDLE     primary = nullptr;
    const bool duplicated =
        DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &primary) != FALSE;
    CloseHandle(token);

    if (!duplicated)
    {
        run.detail = "DuplicateTokenEx: " + std::to_string(GetLastError());
        return run;
    }

    PSID sid = nullptr;
    if (ConvertStringSidToSidW(kLowIntegritySid, &sid) == FALSE)
    {
        run.detail = "ConvertStringSidToSidW: " + std::to_string(GetLastError());
        CloseHandle(primary);
        return run;
    }

    TOKEN_MANDATORY_LABEL label;
    ZeroMemory(&label, sizeof(label));
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = sid;

    const DWORD label_size = static_cast<DWORD>(sizeof(label) + GetLengthSid(sid));
    const bool  lowered = SetTokenInformation(primary, TokenIntegrityLevel, &label, label_size) != FALSE;
    LocalFree(sid);

    if (!lowered)
    {
        run.detail = "SetTokenInformation: " + std::to_string(GetLastError());
        CloseHandle(primary);
        return run;
    }

    const std::wstring command =
        L"\"" + config.launcher_path + L"\" --X-AppBox-ConfigFile \"" + config_path.wstring() + L"\"";
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');

    STARTUPINFOW        startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);

    const bool created =
        CreateProcessAsUserW(primary, nullptr, command_line.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                             nullptr, cwd.c_str(), &startup, &process) != FALSE;
    CloseHandle(primary);

    if (!created)
    {
        run.detail = "CreateProcessAsUserW: " + std::to_string(GetLastError());
        return run;
    }

    WaitForSingleObject(process.hProcess, kStageTimeoutMs);

    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    run.supported = true;
    run.exit_code = exit_code;
    return run;
}

} // namespace

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. A process which is no launcher of the sandbox connects to the control pipe
 *    and sends a start request, which is what the program of the case does.
 *
 * Expected:
 * 1. The request is refused with `unauthorized`: the client runs no launcher of
 *    the sandbox, although its request names the identifier and the root.
 * 2. The pipe keeps serving: the same client reads the environment state of the
 *    sandbox over it, which is the method the sandboxed processes use.
 * 3. The run of the master instance is untouched: it started its own program
 *    only, that program still answers the case, and the run leaves with zero.
 */
TEST_F(E2E_MultiInstance, UnauthorizedClient)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), run_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    nlohmann::json refused = nlohmann::json::object();
    nlohmann::json environment = nlohmann::json::object();
    bool           served = false;

    RawControlPipe pipe(appbox::SandboxPipeName(id));
    ASSERT_TRUE(pipe.IsOpen()) << pipe.LastError();

    appbox::MsgLauncherStart::Req request;
    request.version = appbox::kLauncherStartVersion;
    request.sandbox_id = id;
    request.sandbox_root = appbox::WideToUTF8(cwd.wstring());
    request.selection = "auto";

    ASSERT_TRUE(pipe.Call(1, appbox::MsgLauncherStart::Method, nlohmann::json(request), refused, kAnswerTimeoutMs))
        << pipe.LastError();

    nlohmann::json state;
    state["state"] = "{}";
    served = pipe.Call(2, "Environment", state, environment, kAnswerTimeoutMs);

    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the refused request ended the program of the run";

    master.Exit(0);
    const auto master_exit = master.Wait();

    EXPECT_EQ(master_exit, 0u);

    ASSERT_TRUE(refused.contains("result")) << refused.dump();
    EXPECT_EQ(refused["result"].value("status", std::string()), "refused");
    EXPECT_EQ(refused["result"].value("error_code", std::string()), "unauthorized");

    /* The pipe of the sandbox serves the methods of the sandboxed processes. */
    EXPECT_TRUE(served) << "the control pipe stopped serving the sandbox";
    EXPECT_TRUE(environment.contains("result")) << environment.dump();
}

/**
 * Condition:
 * 1. A launcher serves a sandbox and its own program works.
 * 2. A second launcher of the same archive runs at another integrity level.
 *
 * Expected:
 * 1. The client is refused with the exit code of a refused access and starts no
 *    program, because the level of its token is the one of the sandbox.
 *
 * A machine which does not allow the case to lower the level of a token, or to
 * create the client with that token, skips the case: the decision itself is
 * covered by `Unit_PeerIdentity`.
 */
TEST_F(E2E_MultiInstance, IntegrityMismatch)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());
    const auto run_config = MultiInstanceConfig(cwd, id, "app");

    StartupRunHandle master(cwd.wstring(), run_config);
    ASSERT_TRUE(master.WaitForSessions(kProbeBindQuietMs, kStageTimeoutMs))
        << "the master instance never started its application";

    const auto& master_reports = master.Collect();
    ASSERT_EQ(master_reports.reports.size(), static_cast<std::size_t>(1)) << master_reports.reports.size();

    const auto client =
        RunSecondaryAtLowIntegrity(cwd.wstring(), MultiInstanceRequestDocument(id), std::wstring(kSecondaryConfigName));

    if (!client.supported)
    {
        GTEST_SKIP() << "the case cannot run a client at another integrity level: " << client.detail;
    }

    EXPECT_EQ(master.Ping(), static_cast<std::size_t>(1)) << "the refused client ended the program of the run";

    master.Exit(0);

    EXPECT_EQ(client.exit_code, static_cast<std::uint32_t>(ERROR_ACCESS_DENIED));
    EXPECT_EQ(master.Wait(), 0u);
}

/**
 * Condition:
 * 1. A process of the case occupies the name of the control pipe of a sandbox
 *    and answers a start request as if it were the master instance.
 * 2. A launcher of the sandbox races for the pipe, loses the race and connects
 *    to that process.
 *
 * Expected:
 * 1. The launcher refuses the server before it sends its request, because the
 *    server runs no program of the sandbox: it leaves with the exit code of a
 *    refused access and the process which occupied the name of the pipe
 *    receives no request at all.
 */
TEST_F(E2E_MultiInstance, ServerIdentityMismatch)
{
    const auto cwd = GetCWD();
    const auto id = SandboxIdForPath(GetCWDString());

    /* Requests the server receives, which a refused client sends none of. */
    std::atomic<std::uint32_t> received{ 0 };

    appbox::RemoteAccepter::Options options;
    options.first_instance = true;
    const auto created = appbox::RemoteAccepter::CreateCompetitive(appbox::SandboxPipeName(id), options);
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created) << created.error_code;

    auto accepter = created.accepter;
    accepter->SetSessionCallback([id, &received](const appbox::RemoteSession::Ptr& session) {
        std::weak_ptr<appbox::RemoteSession> weak_session = session;
        session->RegisterMethod(appbox::MsgLauncherStart::Method,
                                [weak_session, id, &received](uint64_t rpc_id, const nlohmann::json&) {
                                    received.fetch_add(1);

                                    appbox::MsgLauncherStart::Rsp response;
                                    response.status = appbox::launcher_start_status::kStarted;
                                    response.sandbox_id = id;
                                    response.pids.push_back(GetCurrentProcessId());

                                    if (auto pipe = weak_session.lock())
                                    {
                                        pipe->SendResponse(rpc_id, nlohmann::json(response));
                                    }
                                });
    });

    ASSERT_TRUE(accepter->Start());

    const auto exit_code =
        ProbeRunRawConfig(cwd.wstring(), MultiInstanceRequestDocument(id), LogRun{}, kSecondaryConfigName);

    accepter->Stop();

    EXPECT_EQ(exit_code, static_cast<std::uint32_t>(ERROR_ACCESS_DENIED));
    EXPECT_EQ(received.load(), 0u) << "the client sent its start request to a process which is no launcher";
}
