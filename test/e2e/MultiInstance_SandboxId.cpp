#include "utils/CommonFixture.hpp"
#include "utils/JoinedThread.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestSandboxId.hpp"
#include "Config.hpp"
#include "SandboxId.hpp"
#include "WString.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

typedef appbox::test::CommonFixture E2E_MultiInstance;
using namespace appbox::test;

namespace
{

/** Prefix of the name of the control pipe of a sandbox. */
constexpr const char* kControlPipePrefix = "appbox_";

/** Exit code of a run which refuses the configuration it was given. */
constexpr std::uint32_t kInvalidData = static_cast<std::uint32_t>(ERROR_INVALID_DATA);

/**
 * @brief Identifier the case of a legal configuration spells out itself.
 *
 * The case names the identifier instead of deriving it, so the run proves that
 * the launcher uses the value of the configuration and not one of its own.
 */
constexpr const char* kKnownSandboxId = "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f";

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts:
 * an empty upper layer and a lower layer below `filesystem`, which is the
 * layout a packaged archive uses.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file and without an identifier.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    return tree.Build();
}

/**
 * @brief Build the configuration of a case which starts one probe process.
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] sandbox_id Identifier the run has to use.
 * @return The configuration with one auto start file.
 */
appbox::LauncherConfig BuildStartupConfig(const std::filesystem::path& root, const std::string& sandbox_id)
{
    auto config = BuildConfig(root);
    config.sandbox_id = sandbox_id;

    appbox::LauncherStartup startup;
    startup.trigger = "app";
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", "app" };
    config.startups.push_back(std::move(startup));

    return config;
}

/**
 * @brief Build the document of a configuration which names one identifier.
 *
 * The document is what the launcher reads, so a case can name a value the type
 * `LauncherConfig` accepts although the launcher refuses it.
 *
 * @param[in] sandbox_id Value of the identifier member.
 * @return The document of a configuration without a startup file.
 */
nlohmann::json DocumentWithId(const std::string& sandbox_id)
{
    nlohmann::json document = appbox::LauncherConfig{};
    document["sandbox_id"] = sandbox_id;
    return document;
}

/**
 * @brief Names of the named pipes of the machine which carry one prefix.
 *
 * The list is a snapshot: the pipe of a run exists while the run lives, so a
 * case takes the snapshot while the launcher is working.
 *
 * @param[in] prefix Prefix the names have to start with, in UTF-8.
 * @return The names, without the `\\.\pipe\` part.
 */
std::vector<std::string> PipeNamesWithPrefix(const std::string& prefix)
{
    std::vector<std::string> names;

    WIN32_FIND_DATAW entry;
    ZeroMemory(&entry, sizeof(entry));

    const auto handle = ::FindFirstFileW(L"\\\\.\\pipe\\*", &entry);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return names;
    }

    do
    {
        const auto name = appbox::WideToUTF8(entry.cFileName);
        if (name.rfind(prefix, 0) == 0)
        {
            names.push_back(name);
        }
    } while (::FindNextFileW(handle, &entry) != FALSE);

    ::FindClose(handle);
    return names;
}

} // namespace

/**
 * Condition:
 * 1. The configuration names a legal identifier of a sandbox.
 * 2. The launcher runs with that configuration.
 *
 * Expected:
 * 1. The launcher accepts the identifier: the run prepares the sandbox, starts
 *    the startup file and exits with zero.
 * 2. The probe process reports its marker, so the application really ran.
 */
TEST_F(E2E_MultiInstance, SandboxIdIsAccepted)
{
    const auto config = BuildStartupConfig(GetCWD(), kKnownSandboxId);

    const auto run = ProbeStartupRun(GetCWDString(), config, "");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));
}

/**
 * Condition:
 * 1. The configuration document carries no identifier of a sandbox at all.
 * 2. The launcher runs with that configuration.
 *
 * Expected:
 * 1. The launcher refuses the run with `ERROR_INVALID_DATA` instead of naming
 *    its sandbox itself.
 * 2. The refusal happens before the state exists: no `data` directory of the
 *    sandbox is created.
 */
TEST_F(E2E_MultiInstance, SandboxIdMissingIsRefused)
{
    nlohmann::json document = appbox::LauncherConfig{};
    document.erase("sandbox_id");

    const auto exit_code = ProbeRunRawConfig(GetCWDString(), document);

    EXPECT_EQ(exit_code, kInvalidData);
    EXPECT_FALSE(std::filesystem::exists(GetCWD() / L"data"));
}

/**
 * Condition:
 * 1. The configuration document carries a value which is no identifier: a
 *    wrong length, upper case digits, a hyphen the layout does not name and a
 *    character which is no hexadecimal digit.
 * 2. The launcher runs with that configuration.
 *
 * Expected:
 * 1. Every value is refused with `ERROR_INVALID_DATA`.
 * 2. No run creates the `data` directory of a sandbox.
 */
TEST_F(E2E_MultiInstance, SandboxIdInvalidIsRefused)
{
    const char* const refused[] = { "appbox",
                                    "2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0",
                                    "2b1f7c4e9a3d4f6b8c2e5d7a1b3c9e0ff",
                                    "2B1F7C4E-9A3D-4F6B-8C2E-5D7A1B3C9E0F",
                                    "2b1f7c4e9-a3d-4f6b-8c2e-5d7a1b3c9e0f",
                                    "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0z" };

    for (const auto* value : refused)
    {
        const auto exit_code = ProbeRunRawConfig(GetCWDString(), DocumentWithId(value));

        EXPECT_EQ(exit_code, kInvalidData) << value;
        EXPECT_FALSE(std::filesystem::exists(GetCWD() / L"data")) << value;
    }
}

/**
 * Condition:
 * 1. The configuration names the identifier the case derives from its working
 *    directory.
 * 2. The launcher runs while the case watches the named pipes of the machine.
 *
 * Expected:
 * 1. The run opens exactly the control pipe of its sandbox, which is
 *    `\\.\pipe\appbox_<id>` with the hyphens of the identifier kept.
 * 2. The run opens no other pipe with that prefix, so the name of the pipe is
 *    derived from the identifier alone.
 */
TEST_F(E2E_MultiInstance, SandboxPipeNameIsUsed)
{
    const auto cwd = GetCWDString();
    const auto id = SandboxIdForPath(cwd);
    ASSERT_TRUE(appbox::IsValidSandboxId(id)) << id;

    const auto config = BuildStartupConfig(GetCWD(), id);

    std::atomic_bool         stop = false;
    std::mutex               mutex;
    std::vector<std::string> observed;

    /*
     * The pipe exists while the launcher works, so the names are collected for
     * as long as the run lasts: the watcher starts before the launcher and stops
     * after it, which covers the whole life of the pipe.
     */
    JoinedThread watcher([&]() {
        while (!stop.load())
        {
            auto names = PipeNamesWithPrefix(kControlPipePrefix);
            {
                std::lock_guard<std::mutex> lock(mutex);
                observed.insert(observed.end(), names.begin(), names.end());
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    const auto run = ProbeStartupRun(cwd, config, "");

    stop = true;
    watcher.Join();

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "app" }));

    const std::string expected = std::string(kControlPipePrefix) + id;

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_FALSE(observed.empty()) << "the run never opened a control pipe";
    EXPECT_NE(std::find(observed.begin(), observed.end(), expected), observed.end());

    for (const auto& name : observed)
    {
        EXPECT_EQ(name, expected) << "the run opened a pipe which is not its control pipe";
    }
}
