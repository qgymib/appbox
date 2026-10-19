#include "utils/ViewPathResolver.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <windows.h>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <nlohmann/json.hpp>
#include "sandbox/utils/Defines.hpp"
#include "utils/WinCall.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Build a fragment which no other case of the run uses.
 * @return The fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(counter++);
}

/**
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        path_ = std::filesystem::temp_directory_path() / (L"appbox-viewpath-" + UniqueFragment());

        std::error_code ec;
        std::filesystem::create_directories(path_, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    /**
     * @brief Get the folder of the case.
     * @return The path of the folder.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Convert a DOS path into the form the resolver of the view uses.
 * @param[in] path Path to convert.
 * @return The path with the namespace of the object manager.
 */
std::string ToNt(const std::filesystem::path& path)
{
    return appbox::WideToUTF8(L"\\??\\" + path.wstring());
}

/**
 * @brief Write a file, creating the folders above it.
 * @param[in] path Path of the file.
 * @param[in] text Content of the file.
 * @return true when the file was written.
 */
bool WriteFile(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    stream << text;
    return stream.good();
}

/**
 * @brief Get the path of the overlay which answers a path of the view.
 *
 * The overlay encodes the drive of the view as its first component, see
 * `MappingPathInSandbox()`.
 *
 * @param[in] root Root folder of the case.
 * @param[in] view Path of the view.
 * @return The path of the overlay.
 */
std::filesystem::path OverlayPath(const std::filesystem::path& root, const std::filesystem::path& view)
{
    const auto drive = view.root_name().wstring();
    return root / L"upper" / drive.substr(0, 1) / view.relative_path();
}

/**
 * @brief The view of a case: the overlay, two lower layers and the host folder.
 *
 * The two lower layers are mapped to the same folder of the view, which is what
 * a patch package produces: the package and the resources of the archive hold
 * the same layer, and the package is mounted first.
 */
struct View
{
    explicit View(const std::filesystem::path& case_root) : root(case_root)
    {
        config.fs_upper = ToNt(root / L"upper");
        config.fs_lower.push_back(
            appbox::SandboxLowerFS{ ToNt(root / L"view"), ToNt(root / L"lower0" / L"#USERPROFILE#") });
        config.fs_lower.push_back(
            appbox::SandboxLowerFS{ ToNt(root / L"view"), ToNt(root / L"lower1" / L"#USERPROFILE#") });
    }

    /**
     * @brief Get the path of the image inside the view.
     * @return The path of the view.
     */
    std::filesystem::path Image() const
    {
        return root / L"view" / L"dir" / L"app.exe";
    }

    /**
     * @brief Get the path of the image inside the overlay.
     * @return The path of the overlay.
     */
    std::filesystem::path Overlay() const
    {
        return OverlayPath(root, Image());
    }

    /**
     * @brief Get the path of the image inside one lower layer.
     * @param[in] index Index of the layer.
     * @return The path of the layer.
     */
    std::filesystem::path Lower(std::size_t index) const
    {
        return root / (L"lower" + std::to_wstring(index)) / L"#USERPROFILE#" / L"dir" / L"app.exe";
    }

    std::filesystem::path root;
    appbox::SandboxConfig config;
};

/**
 * @brief Write the isolation file of a case and name it in the configuration.
 * @param[in,out] view View of the case.
 * @param[in] isolation Token of the isolation mode of the layer.
 * @return true when the file was written.
 */
bool AddIsolation(View& view, const std::string& isolation)
{
    const auto path = view.root / L"isolation.json";

    nlohmann::json entry;
    entry["path"] = "#USERPROFILE#";
    entry["kind"] = "directory";
    entry["isolation"] = isolation;

    nlohmann::json document;
    document["version"] = 1;
    document["entries"] = nlohmann::json::array();
    document["entries"].push_back(entry);

    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            return false;
        }
        stream << document.dump(2);
    }

    view.config.filesystem_isolation_dos_paths.push_back(appbox::WideToUTF8(path.wstring()));
    return true;
}

} // namespace

/**
 * @brief Fixture which prepares the calls the resolver uses.
 *
 * The resolver converts a path of the view into the form of the file system
 * with the helpers of the launcher, which the launcher initializes before it
 * resolves its first path.
 */
class Unit_ViewPathResolver : public ::testing::Test
{
protected:
    /**
     * @brief Initialize the path helpers of the launcher.
     */
    static void SetUpTestSuite()
    {
        appbox::WinCallInit();
    }
};

/**
 * @brief A layer of the view holds the image, so the resolver answers its path.
 */
TEST_F(Unit_ViewPathResolver, TheLowerLayerHoldsTheImage)
{
    TempDir temp;
    View    view(temp.Get());
    ASSERT_TRUE(WriteFile(view.Lower(0), "packed"));

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    ASSERT_TRUE(resolver.Resolve(view.Image().wstring(), host, error)) << error;
    EXPECT_EQ(host, view.Lower(0).wstring());
}

/**
 * @brief The overlay is the layer the view prefers, so it wins over a lower
 *        layer which holds the same path.
 */
TEST_F(Unit_ViewPathResolver, TheOverlayIsPreferred)
{
    TempDir temp;
    View    view(temp.Get());
    ASSERT_TRUE(WriteFile(view.Lower(0), "packed"));
    ASSERT_TRUE(WriteFile(view.Overlay(), "copied up"));

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    ASSERT_TRUE(resolver.Resolve(view.Image().wstring(), host, error)) << error;
    EXPECT_EQ(host, view.Overlay().wstring());
}

/**
 * @brief The lower layers are searched in the order they are mounted, so the
 *        package wins over the resources of the archive.
 */
TEST_F(Unit_ViewPathResolver, TheFirstLowerLayerIsPreferred)
{
    TempDir temp;
    View    view(temp.Get());
    ASSERT_TRUE(WriteFile(view.Lower(0), "package"));
    ASSERT_TRUE(WriteFile(view.Lower(1), "archive"));

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    ASSERT_TRUE(resolver.Resolve(view.Image().wstring(), host, error)) << error;
    EXPECT_EQ(host, view.Lower(0).wstring());
}

/**
 * @brief A path no layer holds is answered by the host filesystem, which is the
 *        last layer of the view.
 */
TEST_F(Unit_ViewPathResolver, TheHostPathIsTheFallback)
{
    TempDir temp;
    View    view(temp.Get());
    ASSERT_TRUE(WriteFile(view.Image(), "host"));

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    ASSERT_TRUE(resolver.Resolve(view.Image().wstring(), host, error)) << error;
    EXPECT_EQ(host, view.Image().wstring());
}

/**
 * @brief The whiteout marker of the overlay hides the image of every layer
 *        below it.
 */
TEST_F(Unit_ViewPathResolver, AWhiteoutMarkerHidesTheImage)
{
    TempDir temp;
    View    view(temp.Get());
    ASSERT_TRUE(WriteFile(view.Lower(0), "packed"));
    ASSERT_TRUE(WriteFile(view.Overlay().wstring() + APPBOX_SANDBOX_WHITEOUT_SUFFIX_W, ""));

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    EXPECT_FALSE(resolver.Resolve(view.Image().wstring(), host, error));
    EXPECT_NE(error.find("whiteout"), std::string::npos) << error;
}

/**
 * @brief The isolation mode of the view hides the layer which holds the image.
 */
TEST_F(Unit_ViewPathResolver, TheIsolationHidesTheLayer)
{
    TempDir temp;
    View    view(temp.Get());
    ASSERT_TRUE(WriteFile(view.Lower(0), "packed"));
    ASSERT_TRUE(AddIsolation(view, "whiteout"));

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    EXPECT_FALSE(resolver.Resolve(view.Image().wstring(), host, error));
    EXPECT_NE(error.find("isolation"), std::string::npos) << error;
}

/**
 * @brief An image which neither a layer nor the host holds is reported instead
 *        of being answered with a path which does not exist.
 */
TEST_F(Unit_ViewPathResolver, AMissingImageIsReported)
{
    TempDir temp;
    View    view(temp.Get());

    const appbox::ViewPathResolver resolver(view.config);

    std::wstring host;
    std::string  error;
    EXPECT_FALSE(resolver.Resolve(view.Image().wstring(), host, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(host.empty());
}
