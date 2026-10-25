#include "utils/CommonFixture.hpp"
#include "utils/LauncherConfigReader.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "src/core/PackService.hpp"
#include "ZipReader.hpp"
#include "launcher/Config.hpp"
#include "WString.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

typedef appbox::test::CommonFixture E2E_LauncherEmbeddedConfig;
using namespace appbox::test;

namespace
{

/**
 * @brief Read the payload of the launcher of the suite.
 * @return The bytes of the launcher, empty when the run named no launcher.
 */
std::vector<char> ReadLauncherPayload()
{
    const auto& path = LauncherPath();
    if (path.empty())
    {
        return {};
    }

    std::string data;
    if (ReadFileFull(path, data) != 0)
    {
        return {};
    }

    return std::vector<char>(data.begin(), data.end());
}

/**
 * @brief Pack a session which starts the 64 bit probe and extract it into a
 *        directory.
 *
 * The probe is imported as `MyApp\app.exe`, so the archive starts it through
 * the layer of the packaged application. The import folder is removed after the
 * extraction, which leaves the archive as the only source of the image.
 *
 * @param[in] root Directory the archive is extracted into.
 * @param[in] trigger Trigger of the startup file.
 * @param[out] error Description of the failure.
 * @return true on success.
 */
bool PackProbeApplication(const std::filesystem::path& root, const std::wstring& trigger, std::string& error)
{
    const auto& launcher = LauncherPath();
    if (launcher.empty())
    {
        error = "the run was started without the path of the launcher";
        return false;
    }

    const auto& probe = ProbePath(ProbeBitness::Bit64);
    if (probe.empty())
    {
        error = "the run was started without the path of the 64 bit probe";
        return false;
    }

    const auto import = root / L"import";
    const auto application = import / L"MyApp";

    std::error_code ec;
    std::filesystem::create_directories(application, ec);
    if (ec)
    {
        error = "failed to create the import folder: " + ec.message();
        return false;
    }

    std::filesystem::copy_file(probe, application / L"app.exe", std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        error = "failed to copy the probe: " + ec.message();
        return false;
    }

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    if (!model.ImportFolder("program_files", application.wstring(), error))
    {
        return false;
    }
    if (!model.RestoreStartupFile("program_files", L"MyApp", L"app.exe", trigger, true, error))
    {
        return false;
    }

    const auto payload = ReadLauncherPayload();
    if (payload.empty())
    {
        error = "failed to read the launcher payload";
        return false;
    }

    appbox::PackPayloads payloads;
    payloads.launcher_bytes = payload.data();
    payloads.launcher_size = payload.size();

    const auto zip_path = root / L"archive.zip";
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     payloads, appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    if (!result.empty())
    {
        error = result;
        return false;
    }

    if (appbox::ExtractArchive(zip_path.wstring(), root.wstring()) != appbox::ZIPREADER_SUCCESS)
    {
        error = "failed to extract the archive";
        return false;
    }

    std::filesystem::remove_all(import, ec);
    return true;
}

} // namespace

/**
 * Condition:
 * 1. A session starts the probe as `MyApp\app.exe`, is packed with the launcher
 *    of the suite and extracted into the directory of the case.
 * 2. The extracted directory carries no `<launcher>.json` beside the program.
 * 3. The extracted launcher runs without `--X-AppBox-ConfigFile`.
 *
 * Expected:
 * 1. The launcher reads the configuration resource of its own image and starts
 *    the application of the archive.
 * 2. The probe reports the marker of the run and the launcher exits with zero.
 * 3. The embedded configuration names the startup file of the model.
 */
TEST_F(E2E_LauncherEmbeddedConfig, EmbeddedConfigurationStartsTheApplication)
{
    std::string error;
    ASSERT_TRUE(PackProbeApplication(GetCWD(), L"probe", error)) << error;

    const auto launcher = GetCWD() / L"app.exe";
    ASSERT_TRUE(std::filesystem::exists(launcher));
    EXPECT_FALSE(std::filesystem::exists(GetCWD() / L"app.exe.json"));

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), launcher.wstring(), "", "embedded");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "embedded" }));

    appbox::LauncherConfig launcher_config;
    ASSERT_TRUE(ReadLauncherConfig(launcher.wstring(), launcher_config, error)) << error;
    ASSERT_EQ(launcher_config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(launcher_config.startups[0].trigger, "probe");
    EXPECT_EQ(launcher_config.startups[0].executable, "#ProgramFiles#\\MyApp\\app.exe");
}

/**
 * Condition:
 * 1. The archive is packed and extracted as in the case above.
 * 2. A configuration file which would refuse the run is written beside the
 *    extracted launcher.
 *
 * Expected:
 * The launcher ignores the file and uses its embedded configuration, so the
 * application starts although the file beside it describes no startup file.
 */
TEST_F(E2E_LauncherEmbeddedConfig, ConfigurationBesideTheLauncherIsIgnored)
{
    std::string error;
    ASSERT_TRUE(PackProbeApplication(GetCWD(), L"probe", error)) << error;

    /*
     * A configuration without a startup file refuses a run. The file is valid
     * JSON, so a launcher which read it would fail for the missing auto start
     * file instead of starting the application.
     */
    {
        appbox::LauncherConfig bogus;
        const auto             json = nlohmann::json(bogus).dump(2);

        std::ofstream file(GetCWD() / L"app.exe.json", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(json.data(), static_cast<std::streamsize>(json.size()));
    }

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), (GetCWD() / L"app.exe").wstring(), "", "embedded");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "embedded" }));
}

/**
 * Condition:
 * 1. The bare launcher of the suite is started: it was not packed, so its image
 *    carries no configuration resource.
 *
 * Expected:
 * 1. The launcher refuses the run with a non zero exit code.
 * 2. No probe process reports, so nothing was started.
 */
TEST_F(E2E_LauncherEmbeddedConfig, MissingConfigurationResourceRefusesTheRun)
{
    const auto& launcher = LauncherPath();
    if (launcher.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), launcher, "", "missing");

    EXPECT_NE(run.exit_code, 0u);
    EXPECT_TRUE(run.started.empty());
}

/**
 * Condition:
 * 1. The trigger and the marker of the run carry characters outside ASCII.
 * 2. The session is packed and the extracted launcher is started with the
 *    non-ASCII trigger.
 *
 * Expected:
 * 1. The configuration resource carries the characters as UTF-8 bytes and not
 *    as `\uXXXX` escapes.
 * 2. The launcher parses the resource, selects the startup file by the trigger
 *    and the probe reports the non-ASCII marker.
 */
TEST_F(E2E_LauncherEmbeddedConfig, Utf8ConfigurationRoundTrips)
{
    const std::wstring trigger = L"启动";
    const std::string  marker = u8"标记";

    std::string error;
    ASSERT_TRUE(PackProbeApplication(GetCWD(), trigger, error)) << error;

    const auto launcher = GetCWD() / L"app.exe";

    std::string json;
    ASSERT_TRUE(ReadLauncherConfigJson(launcher.wstring(), json, error)) << error;
    EXPECT_NE(json.find(u8"启动"), std::string::npos);
    EXPECT_EQ(json.find("\\u"), std::string::npos);

    appbox::LauncherConfig launcher_config;
    ASSERT_TRUE(ReadLauncherConfig(launcher.wstring(), launcher_config, error)) << error;
    ASSERT_EQ(launcher_config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(launcher_config.startups[0].trigger, u8"启动");

    const auto run = ProbeStartupRunEmbedded(GetCWDString(), launcher.wstring(), u8"启动", marker);

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ marker }));
}

/**
 * Condition:
 * 1. The archive is packed and extracted as in the first case.
 * 2. The launcher is started with `--X-AppBox-ConfigFile` and a configuration
 *    whose startup file carries the trigger `override`.
 *
 * Expected:
 * The override wins over the embedded configuration: the startup file of the
 * override runs and the probe reports its marker.
 */
TEST_F(E2E_LauncherEmbeddedConfig, ConfigFileOptionOverridesTheEmbeddedConfiguration)
{
    std::string error;
    ASSERT_TRUE(PackProbeApplication(GetCWD(), L"probe", error)) << error;

    appbox::LauncherConfig  override_config;
    appbox::LauncherStartup startup;
    startup.trigger = "override";
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", "override" };
    override_config.startups.push_back(std::move(startup));

    const auto run =
        ProbeStartupRunConfigFile(GetCWDString(), (GetCWD() / L"app.exe").wstring(), override_config, "override");

    EXPECT_EQ(run.exit_code, 0u);
    EXPECT_EQ(run.started, std::vector<std::string>({ "override" }));
}
