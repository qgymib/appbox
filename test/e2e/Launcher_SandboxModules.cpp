#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/SandboxDll.hpp"
#include "launcher/Config.hpp"
#include "utils/SandboxPaths.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_SandboxModules;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts
 * and one startup file which starts the probe process of the case.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] trigger Trigger and marker of the startup file.
 * @return The configuration with one auto start file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root, const std::string& trigger)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    auto launcher_config = tree.Build();

    appbox::LauncherStartup startup;
    startup.trigger = trigger;
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", trigger };
    launcher_config.startups.push_back(std::move(startup));

    return launcher_config;
}

/**
 * @brief Get the cache directory the launcher releases the modules into.
 * @param[in] root Root directory of the filesystem of the case.
 * @return The absolute path of `data/cache`.
 */
std::filesystem::path CacheDir(const std::filesystem::path& root)
{
    return appbox::SandboxPaths::Resolve(root.wstring()).cache;
}

/**
 * @brief Read a file of the build tree.
 * @param[in] path Path of the file.
 * @return The content of the file, empty when it cannot be read.
 */
std::string ReadFile(const std::wstring& path)
{
    std::string data;
    if (appbox::test::ReadFileFull(path, data) != ERROR_SUCCESS)
    {
        return {};
    }
    return data;
}

/**
 * @brief Check that both released modules are byte for byte the build products.
 *
 * The sizes are compared before the content, because a failure which prints two
 * modules of several megabytes would bury the report of the run.
 *
 * @param[in] cache Cache directory the launcher released the modules into.
 */
void ExpectReleasedModules(const std::filesystem::path& cache)
{
    const struct
    {
        const wchar_t*      name;
        const std::wstring& path;
    } modules[] = {
        { appbox::layout::kSandbox32DllNameW, Sandbox32DllPath() },
        { appbox::layout::kSandbox64DllNameW, Sandbox64DllPath() },
    };

    for (const auto& module : modules)
    {
        const auto released = cache / module.name;
        ASSERT_TRUE(std::filesystem::is_regular_file(released)) << appbox::WideToUTF8(released.wstring());

        const auto expected = ReadFile(module.path);
        ASSERT_FALSE(expected.empty()) << appbox::WideToUTF8(module.path);

        const auto actual = ReadFile(released.wstring());
        EXPECT_EQ(actual.size(), expected.size()) << appbox::WideToUTF8(released.wstring());
        EXPECT_TRUE(actual == expected) << appbox::WideToUTF8(released.wstring());
    }
}

} // namespace

/**
 * Condition:
 * 1. The launcher carries both sandbox injection modules as resources of its
 *    own executable, and the cache directory of the case is empty.
 * 2. The launcher runs with a startup file marked for auto start.
 *
 * Expected:
 * 1. The run starts the startup file inside the sandbox, so both released
 *    modules are usable.
 * 2. The cache directory carries `sandbox32.dll` and `sandbox64.dll`, byte for
 *    byte the modules of the build tree.
 * 3. The resource root carries no module, because the archive no longer holds
 *    them.
 */
TEST_F(E2E_Launcher_SandboxModules, EveryRunReleasesTheModulesIntoTheCache)
{
    ASSERT_TRUE(SandboxModulesAvailable()) << "the sandbox injection modules are unavailable: pass --sandbox32= and "
                                              "--sandbox64=";

    const auto launcher_config = BuildConfig(GetCWD(), "one");

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "one" }));

    ExpectReleasedModules(CacheDir(GetCWD()));

    const std::filesystem::path app(GetCWD() / appbox::layout::kAppDirNameW);
    EXPECT_FALSE(std::filesystem::exists(app / appbox::layout::kSandbox32DllNameW));
    EXPECT_FALSE(std::filesystem::exists(app / appbox::layout::kSandbox64DllNameW));
}

/**
 * Condition:
 * 1. The cache directory of the case carries two files with the names of the
 *    modules and content which is not a module.
 * 2. The launcher runs with a startup file marked for auto start.
 *
 * Expected:
 * 1. The run starts the startup file inside the sandbox, so the content of the
 *    cache was replaced before the injection.
 * 2. Both cached files are byte for byte the modules of the build tree: the
 *    release never reuses the content which is already there.
 */
TEST_F(E2E_Launcher_SandboxModules, ALeftOverModuleIsReplaced)
{
    ASSERT_TRUE(SandboxModulesAvailable()) << "the sandbox injection modules are unavailable: pass --sandbox32= and "
                                              "--sandbox64=";

    const auto launcher_config = BuildConfig(GetCWD(), "one");

    const auto cache = CacheDir(GetCWD());
    ASSERT_TRUE(std::filesystem::create_directories(cache));
    for (const wchar_t* name : { appbox::layout::kSandbox32DllNameW, appbox::layout::kSandbox64DllNameW })
    {
        std::ofstream ofs(cache / name, std::ios::binary | std::ios::trunc);
        ofs << "NOT-A-SANDBOX-MODULE";
    }

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "one" }));

    ExpectReleasedModules(cache);
}

/**
 * Condition:
 * 1. A run already released both modules into the cache directory.
 * 2. The two cached files are held open for reading without sharing the write
 *    access, which is the state a running process keeps a loaded module in.
 * 3. The launcher runs again with a startup file marked for auto start.
 *
 * Expected:
 * 1. The launcher cannot truncate the cached files, reuses them because their
 *    size is the size of the embedded modules and still starts the startup file
 *    inside the sandbox.
 * 2. The cached files still carry the modules of the build tree.
 */
TEST_F(E2E_Launcher_SandboxModules, ALoadedModuleIsReused)
{
    ASSERT_TRUE(SandboxModulesAvailable()) << "the sandbox injection modules are unavailable: pass --sandbox32= and "
                                              "--sandbox64=";

    const auto launcher_config = BuildConfig(GetCWD(), "one");
    const auto cache = CacheDir(GetCWD());

    const auto first = ProbeStartupRun(GetCWDString(), launcher_config, "");
    ASSERT_EQ(first.exit_code, 0u);
    ASSERT_EQ(first.started, std::vector<std::string>({ "one" }));

    HANDLE handles[] = {
        ::CreateFileW((cache / appbox::layout::kSandbox32DllNameW).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr),
        ::CreateFileW((cache / appbox::layout::kSandbox64DllNameW).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr),
    };

    ASSERT_NE(handles[0], INVALID_HANDLE_VALUE);
    ASSERT_NE(handles[1], INVALID_HANDLE_VALUE);

    const auto second = ProbeStartupRun(GetCWDString(), launcher_config, "");

    EXPECT_EQ(second.exit_code, 0u);
    EXPECT_EQ(second.started, std::vector<std::string>({ "one" }));

    ExpectReleasedModules(cache);

    for (HANDLE handle : handles)
    {
        ::CloseHandle(handle);
    }
}

/**
 * Condition:
 * 1. The state directory of the case carries a regular file where the cache
 *    directory belongs, so the directory cannot be created.
 * 2. The launcher runs with a startup file marked for auto start.
 *
 * Expected:
 * 1. The launcher refuses the run, because the modules cannot be released and
 *    nothing can be sandboxed.
 * 2. No startup file is started.
 */
TEST_F(E2E_Launcher_SandboxModules, AnUncreatableCacheRefusesTheRun)
{
    const auto launcher_config = BuildConfig(GetCWD(), "one");

    const auto cache = CacheDir(GetCWD());
    {
        std::ofstream ofs(cache, std::ios::binary | std::ios::trunc);
        ofs << "NOT-A-DIRECTORY";
    }

    const auto run = ProbeStartupRun(GetCWDString(), launcher_config, "");

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty());
}
