#include "WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include "EmbeddedResource.hpp"
#include "src/core/PeResourcePatch.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include "utils/ErrorCodeText.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/WriteFileFull.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Folder of the case, removed when the case ends.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / (L"appbox-resource-" + std::to_wstring(ticks));
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    /**
     * @brief Get the folder path.
     * @return The folder path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Map the launcher image as a data file so its resources are readable.
 *
 * The image is copied into a file of the case first: the products of the build
 * may still be held by a filter between the build and the test, which is not a
 * property of the image.
 *
 * @param[in] directory Folder holding the copy.
 * @param[out] error Reason of a failure.
 * @return The mapped module, nullptr when the run named no launcher or the
 *         image cannot be mapped.
 */
HMODULE OpenLauncher(const TempDir& directory, std::string& error)
{
    const auto& source = appbox::test::LauncherPath();
    if (source.empty())
    {
        error = "the run named no launcher image";
        return nullptr;
    }

    std::string bytes;
    if (appbox::test::ReadFileFull(source, bytes) != ERROR_SUCCESS || bytes.empty())
    {
        error = "the launcher image cannot be read";
        return nullptr;
    }

    const auto copy = (directory.Get() / L"launcher.exe").wstring();
    if (appbox::test::WriteFileFull(copy, bytes) != ERROR_SUCCESS)
    {
        error = "the copy of the launcher image cannot be written";
        return nullptr;
    }

    return appbox::pe_resource::LoadAsDataFile(copy, error);
}

} // namespace

/**
 * @brief A resource of a real image is read as a view into that image, so the
 *        call copies nothing and the payload is the one the build wrote.
 *
 * The launcher carries its two sandbox injection modules as RCDATA resources of
 * its own, which is the payload every run releases into its cache directory.
 */
TEST(Unit_EmbeddedResource, ReadsAResourceOfAnImage)
{
    TempDir     directory;
    std::string error;
    const auto  module = OpenLauncher(directory, error);
    if (module == nullptr)
    {
        GTEST_SKIP() << error;
    }
    const appbox::pe_resource::LoadedImage image(module);

    const auto sandbox = appbox::ReadEmbeddedResource(module, IDR_APPBOX_SANDBOX64);
    ASSERT_TRUE(sandbox) << appbox::test::DescribeErrorCode(sandbox.error());
    EXPECT_FALSE(sandbox->empty());

    /* The resource is a program module, so the view starts with its image. */
    ASSERT_GE(sandbox->size(), static_cast<std::size_t>(2));
    EXPECT_EQ(sandbox->substr(0, 2), "MZ");
}

/**
 * @brief The run configuration is a resource the packer writes, so a launcher
 *        which was never packed carries none of it.
 */
TEST(Unit_EmbeddedResource, ABareLauncherCarriesNoConfiguration)
{
    TempDir     directory;
    std::string error;
    const auto  module = OpenLauncher(directory, error);
    if (module == nullptr)
    {
        GTEST_SKIP() << error;
    }
    const appbox::pe_resource::LoadedImage image(module);

    const auto config = appbox::ReadEmbeddedResource(module, IDR_APPBOX_LAUNCHER_CONFIG);
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error(), appbox::ErrorCode::kEmbeddedResourceMissing);
}

/**
 * @brief The test executable declares the identifiers of the launcher but
 *        carries none of its payload, which is a missing resource.
 */
TEST(Unit_EmbeddedResource, ARunTimeImageCarriesNoPayload)
{
    const auto missing = appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_SANDBOX64);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), appbox::ErrorCode::kEmbeddedResourceMissing);
}
