#include "RemoteAccepter.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "BuildCommandLine.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include "probe/Bind.hpp"
#include "probe/StartupStarted.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"

typedef appbox::test::CommonFixture E2E_Probe;
using namespace appbox::test;

namespace
{

/** Exit code the case gives the program it terminates. */
constexpr std::uint32_t kTerminatedExitCode = 3;

/** Budget of the wait for a program the case terminated, in milliseconds. */
constexpr std::uint32_t kProgramExitTimeoutMs = 30000;

/** Budget of the connection of a probe the case started itself, in milliseconds. */
constexpr std::uint32_t kBindTimeoutMs = 30000;

/** Delay between two polls of the state of a probe, in milliseconds. */
constexpr std::uint32_t kPollDelayMs = 10;

/** Budget of the wait for a probe which lost the pipe of its case, in milliseconds. */
constexpr std::uint32_t kProbeExitTimeoutMs = 10000;

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the layout of a packed archive: the state of the
 * sandbox below `data`. It names no startup file, so the handle starts the probe
 * once, like the single main program of an application.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration of the run.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {})
    });
    /* clang-format on */

    return tree.Build();
}

/**
 * @brief Build the name of a pipe which belongs to the case alone.
 * @return The name of the pipe, for example `\\.\pipe\appbox-test-1-Ab3d`.
 */
std::string BuildCasePipeName()
{
    return R"(\\.\pipe\appbox-test-)" + std::to_string(std::time(nullptr)) + "-" + appbox::RandomString(16);
}

} // namespace

/**
 * Condition:
 * 1. A case asks one probe process two questions through one handle.
 *
 * Expected:
 * 1. Both answers name the same process, so the probe served the second
 *    question instead of being started again: a probe is a service of the case
 *    and not a task which ends with its first answer.
 */
TEST_F(E2E_Probe, ServesTwoRequestsInOneProcess)
{
    const auto launcher_config = BuildConfig(GetCWD());

    Probe probe(GetCWD(), launcher_config);

    const auto first = probe.Call<ProtocolStartupStarted>(ProtocolStartupStarted::Req{});
    const auto second = probe.Call<ProtocolStartupStarted>(ProtocolStartupStarted::Req{});

    ASSERT_NE(first.pid, 0u);
    EXPECT_EQ(first.pid, second.pid) << "the two answers came from different processes";
    EXPECT_EQ(first.pid, probe.Pid()) << "the answer does not name the process of the handle";
    EXPECT_EQ(first.cwd, second.cwd) << "the two answers describe different directories";
}

/**
 * Condition:
 * 1. A case leaves the scope of a handle without ending the probe itself.
 *
 * Expected:
 * 1. The probe process ends with the handle, so a case which returns early
 *    leaves no process behind and the launcher of the run returns.
 */
TEST_F(E2E_Probe, DestructorEndsTheRun)
{
    const auto launcher_config = BuildConfig(GetCWD());

    std::uint32_t pid = 0;
    {
        Probe probe(GetCWD(), launcher_config);
        pid = probe.Call<ProtocolStartupStarted>(ProtocolStartupStarted::Req{}).pid;
    }

    ASSERT_NE(pid, 0u);

    bool          ended = false;
    std::uint32_t open_error = 0;
    HANDLE        process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process != nullptr)
    {
        ended = WaitForSingleObject(process, 10000) == WAIT_OBJECT_0;
        CloseHandle(process);
    }
    else
    {
        /*
         * The object of the process is gone as well, which is the strongest form
         * of a process which ended: a process which still runs keeps its object,
         * because the launcher of the run holds a handle of every program it
         * started until it leaves itself.
         */
        open_error = GetLastError();
        ended = open_error == ERROR_INVALID_PARAMETER;
    }

    EXPECT_TRUE(ended) << "the probe process outlived the handle of the case: " << open_error;
}

/**
 * Condition:
 * 1. A case starts one probe process and terminates it, which is what a master
 *    instance does to a program of a request it has to roll back.
 * 2. The case then asks the run of the probe to report, which is what the
 *    helper of a second instance does once its request was answered.
 *
 * Expected:
 * 1. The run reports nothing and the case is not refused: a program which left
 *    has nothing to report, and the absence of its answer is no defect of the
 *    run. A refused request of a master instance terminates the program it
 *    already started, so a case which asked such a run would otherwise fail for
 *    a program the sandbox removed on purpose.
 */
TEST_F(E2E_Probe, LeftProgramIsNotAsked)
{
    const auto launcher_config = BuildConfig(GetCWD());

    Probe probe(GetCWD(), launcher_config);

    const auto pid = probe.Call<ProtocolStartupStarted>(ProtocolStartupStarted::Req{}).pid;
    ASSERT_NE(pid, 0u);

    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    ASSERT_NE(process, nullptr) << "the program of the run cannot be opened: " << GetLastError();

    EXPECT_TRUE(TerminateProcess(process, kTerminatedExitCode))
        << "the program of the run cannot be terminated: " << GetLastError();
    EXPECT_EQ(WaitForSingleObject(process, kProgramExitTimeoutMs), WAIT_OBJECT_0)
        << "the program of the run did not leave";
    CloseHandle(process);

    std::vector<ProtocolStartupStarted::Rsp> reports;
    EXPECT_NO_THROW(reports = probe.Run().CallAll<ProtocolStartupStarted>(ProtocolStartupStarted::Req{}));
    EXPECT_TRUE(reports.empty()) << "a program which left answered the request of the case";
}

/**
 * Condition:
 * 1. A case starts one probe process on a pipe of its own, so the process is
 *    the only program which serves the case.
 * 2. The case closes that pipe without asking the probe to leave, which is what
 *    the end of the test executable does to every probe of a run.
 *
 * Expected:
 * 1. The probe process leaves with the pipe of its case, so an abnormal end of
 *    the test executable leaves neither a probe nor a launcher behind: a probe
 *    which kept waiting would keep the launcher of its run alive, and that
 *    launcher holds the registry hive of the sandbox, which the artifact of a
 *    failed run has to carry.
 */
TEST_F(E2E_Probe, LeavesWhenTheCaseIsGone)
{
    auto bound = std::make_shared<std::atomic_bool>(false);

    const auto pipe = BuildCasePipeName();
    auto       accepter = appbox::RemoteAccepter::Create(pipe);
    accepter->SetSessionCallback([bound](const appbox::RemoteSession::Ptr& session) {
        std::weak_ptr<appbox::RemoteSession> weak_session = session;

        session->RegisterMethod(appbox::test::ProtocolBind::Method,
                                [bound, weak_session](uint64_t id, const nlohmann::json&) {
                                    auto bound_session = weak_session.lock();
                                    if (!bound_session)
                                    {
                                        return;
                                    }

                                    *bound = true;
                                    bound_session->SendResponse(id, nlohmann::json(appbox::test::ProtocolBind::Rsp{}));
                                });
    });

    ASSERT_TRUE(accepter->Start()) << "the pipe of the case cannot be served";

    const auto& probe_path = ProbePath(ProbeBitness::Bit64);
    ASSERT_FALSE(probe_path.empty()) << "the 64 bit probe is unavailable";

    const std::vector<std::wstring> args = { L"probe", L"--probe_pipe", appbox::UTF8ToWide(pipe), L"--probe_key",
                                             L"case" };
    auto                            command_line = appbox::BuildCommandLine(probe_path, args);

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    ASSERT_TRUE(CreateProcessW(probe_path.c_str(), command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup_info, &process_info))
        << "the probe cannot be started: " << GetLastError();
    CloseHandle(process_info.hThread);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kBindTimeoutMs);
    while (!bound->load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    EXPECT_TRUE(bound->load()) << "the probe never connected to the pipe of the case";

    accepter->Stop();

    const auto waited = WaitForSingleObject(process_info.hProcess, kProbeExitTimeoutMs);
    if (waited != WAIT_OBJECT_0)
    {
        TerminateProcess(process_info.hProcess, 1);
        WaitForSingleObject(process_info.hProcess, 5000);
    }

    CloseHandle(process_info.hProcess);

    EXPECT_EQ(waited, WAIT_OBJECT_0) << "the probe outlived the pipe of the case";
}
