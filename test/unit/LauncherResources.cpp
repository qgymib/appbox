#include "src/core/PeResourcePatch.hpp"
#include <gtest/gtest.h>
#include "src/core/LauncherResources.hpp"
#include "EmbeddedResource.hpp"
#include "launcher/Config.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include "utils/LauncherConfigReader.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "Test.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/**
 * @brief Read the payload of the 64 bit probe.
 *
 * The probe is a real program image, which is what the resource update API of
 * Windows needs: a payload which is not a PE image cannot carry a resource.
 *
 * @return The bytes of the probe, empty when the run named no probe.
 */
std::vector<char> ProbePayload()
{
    const auto path = appbox::test::ProbePath(appbox::test::ProbeBitness::Bit64);
    if (path.empty())
    {
        return {};
    }

    std::string data;
    if (appbox::test::ReadFileFull(path, data) != 0)
    {
        return {};
    }

    return std::vector<char>(data.begin(), data.end());
}

/** Counter of the resources a callback enumerated. */
struct ResourceNameCount
{
    int config = 0;
    int image = 0;
};

/**
 * @brief Count the resources which carry the identifiers of the launcher.
 */
BOOL CALLBACK CountLauncherResource(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param)
{
    const auto id = reinterpret_cast<ULONG_PTR>(name);
    if (id == static_cast<ULONG_PTR>(IDR_APPBOX_LAUNCHER_CONFIG))
    {
        ++reinterpret_cast<ResourceNameCount*>(param)->config;
    }
    if (id == static_cast<ULONG_PTR>(IDR_APPBOX_STARTUP_IMAGE))
    {
        ++reinterpret_cast<ResourceNameCount*>(param)->image;
    }
    return TRUE;
}

} // namespace

TEST(Unit_LauncherResources, BuildsUtf8JsonWithoutBom)
{
    appbox::LauncherConfig config;
    config.hide_console = true;
    config.startup.screen = false;

    appbox::LauncherStartup startup;
    startup.trigger = u8"启动";
    startup.auto_start = true;
    startup.executable = u8"#ProgramFiles#\\应用\\app.exe";
    startup.arguments = { u8"--标记=值" };
    config.startups.push_back(startup);

    const auto json = appbox::BuildLauncherConfigJson(config);

    ASSERT_FALSE(json.empty());

    /* The document starts with `{`, not with the three bytes of a UTF-8 BOM. */
    EXPECT_NE(json.compare(0, 3, "\xEF\xBB\xBF"), 0);

    /* A character outside ASCII is kept as its UTF-8 bytes. */
    EXPECT_NE(json.find(u8"启动"), std::string::npos);
    EXPECT_NE(json.find(u8"--标记=值"), std::string::npos);

    const auto parsed = nlohmann::json::parse(json).get<appbox::LauncherConfig>();
    EXPECT_TRUE(parsed.hide_console);
    EXPECT_FALSE(parsed.startup.screen);
    ASSERT_EQ(parsed.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(parsed.startups[0].trigger, u8"启动");
    EXPECT_TRUE(parsed.startups[0].auto_start);
    EXPECT_EQ(parsed.startups[0].executable, u8"#ProgramFiles#\\应用\\app.exe");
    ASSERT_EQ(parsed.startups[0].arguments.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(parsed.startups[0].arguments[0], u8"--标记=值");

    /* The startup image is not part of the document: it is a resource of its own. */
    EXPECT_EQ(json.find("\"image\""), std::string::npos);
}

TEST(Unit_LauncherResources, EmbedsTheConfigurationAsTheResource)
{
    const auto payload = ProbePayload();
    ASSERT_FALSE(payload.empty()) << "the 64 bit probe is unavailable";

    appbox::LauncherConfig  config;
    appbox::LauncherStartup startup;
    startup.trigger = "probe";
    startup.executable = "#ProgramFiles#\\MyApp\\app.exe";
    config.startups.push_back(startup);

    const auto json = appbox::BuildLauncherConfigJson(config);

    std::string error;
    const auto  patched =
        appbox::EmbedLauncherResources(payload.data(), payload.size(), json, std::string_view(), error);
    ASSERT_FALSE(patched.empty()) << error;

    const auto                               path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-LauncherConfig");
    const appbox::pe_resource::TemporaryFile file(path);
    ASSERT_TRUE(appbox::pe_resource::WriteFileBytes(path, patched.data(), patched.size(), error)) << error;

    std::string read;
    ASSERT_TRUE(appbox::test::ReadLauncherConfigJson(path.wstring(), read, error)) << error;
    EXPECT_EQ(read, json);

    appbox::LauncherConfig parsed;
    ASSERT_TRUE(appbox::test::ReadLauncherConfig(path.wstring(), parsed, error)) << error;
    ASSERT_EQ(parsed.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(parsed.startups[0].trigger, "probe");
    EXPECT_EQ(parsed.startups[0].executable, "#ProgramFiles#\\MyApp\\app.exe");
}

TEST(Unit_LauncherResources, EmbedsTheStartupImageAsTheResource)
{
    const auto payload = ProbePayload();
    ASSERT_FALSE(payload.empty()) << "the 64 bit probe is unavailable";

    appbox::LauncherConfig config;

    /* A distinctive payload: the bytes of a real image are not needed here. */
    const std::string image = "\x89PNG\r\n\x1a\n-image";

    std::string error;
    const auto  patched = appbox::EmbedLauncherResources(payload.data(), payload.size(),
                                                         appbox::BuildLauncherConfigJson(config), image, error);
    ASSERT_FALSE(patched.empty()) << error;

    const auto                               path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-LauncherImage");
    const appbox::pe_resource::TemporaryFile file(path);
    ASSERT_TRUE(appbox::pe_resource::WriteFileBytes(path, patched.data(), patched.size(), error)) << error;

    std::string bytes;
    ASSERT_TRUE(appbox::test::ReadLauncherResource(path.wstring(), IDR_APPBOX_STARTUP_IMAGE, bytes, error)) << error;
    EXPECT_EQ(bytes, image);

    /* The configuration is written by the same update session. */
    appbox::LauncherConfig parsed;
    ASSERT_TRUE(appbox::test::ReadLauncherConfig(path.wstring(), parsed, error)) << error;
    EXPECT_TRUE(parsed.startup.screen);
}

TEST(Unit_LauncherResources, ReplacesAnEarlierConfiguration)
{
    const auto payload = ProbePayload();
    ASSERT_FALSE(payload.empty()) << "the 64 bit probe is unavailable";

    appbox::LauncherConfig  first_config;
    appbox::LauncherStartup first_startup;
    first_startup.trigger = "first";
    first_config.startups.push_back(first_startup);

    appbox::LauncherConfig  second_config;
    appbox::LauncherStartup second_startup;
    second_startup.trigger = "second";
    second_config.startups.push_back(second_startup);

    std::string error;
    const auto  first = appbox::EmbedLauncherResources(
        payload.data(), payload.size(), appbox::BuildLauncherConfigJson(first_config), std::string_view(), error);
    ASSERT_FALSE(first.empty()) << error;

    const auto second = appbox::EmbedLauncherResources(
        first.data(), first.size(), appbox::BuildLauncherConfigJson(second_config), std::string_view(), error);
    ASSERT_FALSE(second.empty()) << error;

    const auto                               path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-LauncherConfig");
    const appbox::pe_resource::TemporaryFile file(path);
    ASSERT_TRUE(appbox::pe_resource::WriteFileBytes(path, second.data(), second.size(), error)) << error;

    /*
     * The payload carries exactly one configuration resource: a second copy
     * would make the lookup depend on the order of the resource directory.
     */
    const auto module = appbox::pe_resource::LoadAsDataFile(path.wstring(), error);
    ASSERT_NE(module, nullptr) << error;
    {
        const appbox::pe_resource::LoadedImage image(module);
        ResourceNameCount                      count;
        ASSERT_TRUE(EnumResourceNamesW(module, RT_RCDATA, CountLauncherResource, reinterpret_cast<LONG_PTR>(&count)));
        EXPECT_EQ(count.config, 1);
        EXPECT_EQ(count.image, 0);
    }

    appbox::LauncherConfig parsed;
    ASSERT_TRUE(appbox::test::ReadLauncherConfig(path.wstring(), parsed, error)) << error;
    ASSERT_EQ(parsed.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(parsed.startups[0].trigger, "second");
}

TEST(Unit_LauncherResources, RefusesAPayloadWhichIsNotAnImage)
{
    std::string error;
    const auto  patched = appbox::EmbedLauncherResources("NOT-A-PE-IMAGE", 14, "{}", std::string_view(), error);
    EXPECT_TRUE(patched.empty());
    EXPECT_NE(error.find("PE"), std::string::npos);
}

TEST(Unit_LauncherResources, RefusesAnEmptyConfiguration)
{
    const auto payload = ProbePayload();
    ASSERT_FALSE(payload.empty()) << "the 64 bit probe is unavailable";

    std::string error;
    const auto  patched = appbox::EmbedLauncherResources(payload.data(), payload.size(), "", std::string_view(), error);
    EXPECT_TRUE(patched.empty());
    EXPECT_NE(error.find("empty"), std::string::npos);
}
