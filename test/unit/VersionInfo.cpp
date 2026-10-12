#include <gtest/gtest.h>
#include "utils/VersionInfo.hpp"
#include "utils/LauncherPath.hpp"
#include "utils/RealFsFolder.hpp"
#include "src/core/ApplicationMetadata.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Read a whole file.
 * @param[in] path File to read.
 * @return The content of the file, empty when it cannot be read.
 */
std::vector<char> ReadAllBytes(const std::wstring& path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const auto size = file.tellg();
    if (size < 0)
    {
        return {};
    }

    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    return file ? bytes : std::vector<char>();
}

/**
 * @brief Write a whole file.
 * @param[in] path File to write.
 * @param[in] bytes Content of the file.
 * @return true on success.
 */
bool WriteAllBytes(const std::wstring& path, const std::vector<char>& bytes)
{
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    if (!bytes.empty())
    {
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    return file.good();
}

/**
 * @brief Build a copy of the launcher which carries file properties.
 *
 * The packer writes the file properties of the packaged application into the
 * launcher payload, so the case patches a copy of the real launcher: the name
 * the launcher reads at run time is the name of that application.
 *
 * @param[in] folder Folder which receives the copy.
 * @param[in] name File name of the copy.
 * @param[in] fields Fields of the version resource.
 * @param[out] path Path of the copy.
 * @return true when the copy was written.
 */
bool MakeProgramWithProperties(const appbox::test::RealFsFolder& folder, const std::wstring& name,
                               const std::vector<appbox::MetadataField>& fields, std::wstring& path)
{
    const auto& launcher = appbox::test::LauncherPath();
    if (launcher.empty())
    {
        return false;
    }

    const auto bytes = ReadAllBytes(launcher);
    if (bytes.empty())
    {
        return false;
    }

    appbox::ApplicationVersionInfo info;
    info.fields = fields;

    std::string warning;
    const auto  patched = appbox::ApplyApplicationMetadata(bytes.data(), bytes.size(), info, warning);
    if (patched.empty())
    {
        return false;
    }

    if (!folder.WriteFile(name, std::string(patched.data(), patched.size())))
    {
        return false;
    }

    path = (folder.Get() / name).wstring();
    return true;
}

} // namespace

TEST(Unit_VersionInfo, ReadsTheProductNameOfAProgram)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-version-info-product");

    std::wstring path;
    ASSERT_TRUE(MakeProgramWithProperties(folder, L"app.exe",
                                          {
                                              { appbox::metadata_field::kProductName,     L"My Application" },
                                              { appbox::metadata_field::kFileDescription, L"My App"         }
    },
                                          path));

    EXPECT_EQ(appbox::ReadImageDisplayName(path), L"My Application");
}

TEST(Unit_VersionInfo, FallsBackToTheFileDescription)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-version-info-description");

    std::wstring path;
    ASSERT_TRUE(MakeProgramWithProperties(folder, L"app.exe",
                                          {
                                              { appbox::metadata_field::kFileDescription, L"My App" }
    },
                                          path));

    EXPECT_EQ(appbox::ReadImageDisplayName(path), L"My App");
}

TEST(Unit_VersionInfo, FallsBackToTheFileNameWithoutItsExtension)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-version-info-name");
    ASSERT_TRUE(folder.WriteFile(L"plain.exe", "not a program at all"));

    /* A file without a version resource still has a name to show. */
    EXPECT_EQ(appbox::ReadImageDisplayName((folder.Get() / L"plain.exe").wstring()), L"plain");
}

TEST(Unit_VersionInfo, AnEmptyPathHasNoName)
{
    EXPECT_TRUE(appbox::ReadImageDisplayName(L"").empty());
}

TEST(Unit_VersionInfo, TheRunningProgramReportsItsOwnName)
{
    const auto& launcher = appbox::test::LauncherPath();
    if (launcher.empty())
    {
        GTEST_SKIP() << "the run was started without the path of the launcher";
    }

    /*
     * The launcher carries no version resource of its own, so its own name is
     * the fallback: the file name of the running program.
     */
    EXPECT_EQ(appbox::ReadImageDisplayName(launcher), std::filesystem::path(launcher).stem().wstring());
}
