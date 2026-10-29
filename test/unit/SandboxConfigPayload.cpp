#include "src/core/PeResourcePatch.hpp" /* Must be first include file: it brings <windows.h> in. */
#include <detours.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "sandbox/Config.hpp"
#include "sandbox/utils/Defines.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include "src/core/LauncherResources.hpp"
#include "src/core/SandboxConfigPayload.hpp"
#include "utils/LauncherConfigReader.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/ReadFileFull.hpp"
#include "Test.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Read the payload of the launcher of the suite.
 *
 * The configuration is a payload of a real program image, so the cases work on
 * the launcher the run named instead of on a synthetic file: the packer writes
 * it into exactly this image.
 *
 * @return The bytes of the launcher, empty when the run named no launcher.
 */
std::vector<char> LauncherPayload()
{
    const auto& path = appbox::test::LauncherPath();
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

/**
 * @brief Count the payloads an image carries under the GUID of the run.
 *
 * @param[in] image Image to inspect.
 * @return Number of payloads of that GUID.
 */
int CountConfigPayloads(const std::vector<char>& image)
{
    const auto path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-SandboxConfigCount");
    const appbox::pe_resource::TemporaryFile file(path);

    std::string error;
    if (!appbox::pe_resource::WriteFileBytes(path, image.data(), image.size(), error))
    {
        return -1;
    }

    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return -1;
    }

    PDETOUR_BINARY binary = DetourBinaryOpen(handle);
    CloseHandle(handle);
    if (binary == nullptr)
    {
        return -1;
    }

    int        count = 0;
    GUID       guid{};
    DWORD      size = 0;
    DWORD      iterator = 0;
    const GUID wanted = APPBOX_SANDBOX_GUID;
    while (DetourBinaryEnumeratePayloads(binary, &guid, &size, &iterator) != nullptr)
    {
        if (memcmp(&guid, &wanted, sizeof(GUID)) == 0)
        {
            ++count;
        }
    }

    DetourBinaryClose(binary);
    return count;
}

/**
 * @brief Read the configuration an image carries through the binary API of Detours.
 *
 * @param[in] image Image to inspect.
 * @param[out] config The configuration, unchanged when the image carries none.
 * @return true when the image carries a configuration of this build.
 */
bool ReadConfig(const std::vector<char>& image, appbox::SandboxConfig& config)
{
    const auto path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-SandboxConfigRead");
    const appbox::pe_resource::TemporaryFile file(path);

    std::string error;
    if (!appbox::pe_resource::WriteFileBytes(path, image.data(), image.size(), error))
    {
        return false;
    }

    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    PDETOUR_BINARY binary = DetourBinaryOpen(handle);
    CloseHandle(handle);
    if (binary == nullptr)
    {
        return false;
    }

    const GUID  guid = APPBOX_SANDBOX_GUID;
    DWORD       size = 0;
    const void* payload = DetourBinaryFindPayload(binary, guid, &size);

    bool found = false;
    if (payload != nullptr && size != 0)
    {
        try
        {
            const std::string text(static_cast<const char*>(payload), size);
            config = nlohmann::json::parse(text).get<appbox::SandboxConfig>();
            found = true;
        }
        catch (const std::exception&)
        {
            found = false;
        }
    }

    DetourBinaryClose(binary);
    return found;
}

} // namespace

/**
 * Condition:
 * 1. The payload of the launcher of the suite is patched by the packer step.
 *
 * Expected:
 * 1. The step reports no error and returns an image which carries the
 *    configuration of the sandbox under the GUID of the injection data.
 * 2. The configuration turns the hooks off, which is what makes the injection
 *    module leave the process alone.
 */
TEST(Unit_SandboxConfigPayload, WritesTheConfigurationWithTheHooksOff)
{
    const auto payload = LauncherPayload();
    if (payload.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    std::string error;
    const auto  patched = appbox::EmbedSandboxConfigPayload(payload.data(), payload.size(), error);
    ASSERT_FALSE(patched.empty()) << error;
    EXPECT_TRUE(error.empty());

    appbox::SandboxConfig config;
    ASSERT_TRUE(ReadConfig(patched, config)) << "the patched image carries no configuration of the run";
    EXPECT_FALSE(config.hook_enabled);
}

/**
 * Condition:
 * 1. The configuration a run writes for a process it creates is built by the
 *    launcher with the default member.
 *
 * Expected:
 * 1. The document enables the hooks, so a process of the run is isolated.
 * 2. The document survives a round trip through JSON, so the writer and the
 *    reader of `sandbox/Config.hpp` agree.
 */
TEST(Unit_SandboxConfigPayload, TheRuntimeConfigurationEnablesTheHooks)
{
    const appbox::SandboxConfig config;
    EXPECT_TRUE(config.hook_enabled);

    const auto document = nlohmann::json(config);
    ASSERT_TRUE(document.contains("hook_enabled"));
    EXPECT_TRUE(document["hook_enabled"].get<bool>());

    const auto restored = document.get<appbox::SandboxConfig>();
    EXPECT_TRUE(restored.hook_enabled);
}

/**
 * Condition:
 * 1. An image is patched twice, which is what a packer step which runs on an
 *    image it already patched would do.
 *
 * Expected:
 * 1. The image carries exactly one payload of the GUID of the run, because the
 *    reader takes the first record it finds.
 */
TEST(Unit_SandboxConfigPayload, ReplacesThePayloadInsteadOfAddingASecond)
{
    const auto payload = LauncherPayload();
    if (payload.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    std::string error;
    const auto  once = appbox::EmbedSandboxConfigPayload(payload.data(), payload.size(), error);
    ASSERT_FALSE(once.empty()) << error;

    const auto twice = appbox::EmbedSandboxConfigPayload(once.data(), once.size(), error);
    ASSERT_FALSE(twice.empty()) << error;

    EXPECT_EQ(CountConfigPayloads(twice), 1);
}

/**
 * Condition:
 * 1. The run configuration is embedded as a resource and the sandbox
 *    configuration is written afterwards, which is the order of the packer.
 *
 * Expected:
 * 1. The resource of the patched image is the document the packer wrote: the
 *    configuration is a section of its own and leaves the resource directory
 *    alone.
 */
TEST(Unit_SandboxConfigPayload, KeepsTheResourcesOfTheImage)
{
    const auto payload = LauncherPayload();
    if (payload.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    const std::string json = "{\"sandbox_id\":\"sandbox-config-payload\"}";

    std::string error;
    const auto  with_resource = appbox::EmbedLauncherResources(payload.data(), payload.size(), json, {}, error);
    ASSERT_FALSE(with_resource.empty()) << error;

    const auto patched = appbox::EmbedSandboxConfigPayload(with_resource.data(), with_resource.size(), error);
    ASSERT_FALSE(patched.empty()) << error;

    const auto                               path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-SandboxConfigRes");
    const appbox::pe_resource::TemporaryFile file(path);
    ASSERT_TRUE(appbox::pe_resource::WriteFileBytes(path, patched.data(), patched.size(), error)) << error;

    std::string resource;
    ASSERT_TRUE(appbox::test::ReadLauncherResource(path.wstring(), IDR_APPBOX_LAUNCHER_CONFIG, resource, error))
        << error;
    EXPECT_EQ(resource, json);
}

/**
 * Condition:
 * 1. A buffer which is no PE image is patched.
 *
 * Expected:
 * 1. The step fails and reports the reason, so the packer refuses the run
 *    instead of writing a launcher which the sandbox would isolate.
 */
TEST(Unit_SandboxConfigPayload, RejectsAPayloadWhichIsNoPeImage)
{
    const std::string text = "this is no image";

    std::string error;
    EXPECT_TRUE(appbox::EmbedSandboxConfigPayload(text.data(), text.size(), error).empty());
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_TRUE(appbox::EmbedSandboxConfigPayload(nullptr, 0, error).empty());
    EXPECT_FALSE(error.empty());
}
