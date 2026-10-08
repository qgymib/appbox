#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
/*
 * The resource API is used with the wide character forms: the RT_VERSION macro
 * expands to its ANSI form unless UNICODE is defined, and the targets of the
 * packer do not define it.
 */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include "src/core/ApplicationMetadata.hpp"
#include "src/core/PackModel.hpp"
#include "utils/LauncherPath.hpp"
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper creating a unique folder below the temporary directory.
 */
class TempDir
{
public:
    TempDir()
    {
        path_ = std::filesystem::temp_directory_path() / (L"appbox-metadata-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

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
 * @brief Create a folder below a parent directory.
 * @param[in] parent Parent directory.
 * @param[in] name Folder name.
 * @return The created folder path.
 */
std::filesystem::path MakeFolder(const std::filesystem::path& parent, const std::wstring& name)
{
    const auto folder = parent / name;
    std::filesystem::create_directories(folder);
    return folder;
}

/**
 * @brief Create a file with content below a parent directory.
 *
 * The startup file list of the model only checks that the referenced file
 * exists, so the content of the fixtures does not have to be an executable.
 *
 * @param[in] parent Parent directory.
 * @param[in] name File name.
 * @param[in] content File content.
 * @return The created file path.
 */
std::filesystem::path MakeFile(const std::filesystem::path& parent, const std::wstring& name,
                               const std::string& content)
{
    const auto file = parent / name;
    std::filesystem::create_directories(file.parent_path());

    FILE* handle = nullptr;
    if (_wfopen_s(&handle, file.wstring().c_str(), L"wb") != 0 || handle == nullptr)
    {
        return file;
    }
    fwrite(content.data(), 1, content.size(), handle);
    fclose(handle);
    return file;
}

/**
 * @brief Get the path of the running test executable.
 * @return The path of the test executable.
 */
std::wstring SelfPath()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const auto           length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::wstring(buffer.data(), length);
}

/**
 * @brief Copy a file.
 * @param[in] source Source path.
 * @param[in] destination Destination path.
 * @return true on success.
 */
bool CopyFileTo(const std::wstring& source, const std::wstring& destination)
{
    return CopyFileW(source.c_str(), destination.c_str(), FALSE) != FALSE;
}

/**
 * @brief Read a whole file.
 * @param[in] path File path.
 * @return The file content, empty on failure.
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
 * @param[in] path File path.
 * @param[in] bytes File content.
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

    file.close();
    return file ? true : false;
}

/**
 * @brief Callback counting the resources of one type.
 *
 * @param[in] module Module of the enumeration, unused.
 * @param[in] type Resource type of the enumeration, unused.
 * @param[in] name Resource name or id, unused.
 * @param[in] param The `std::size_t` counting the resources.
 * @return TRUE to continue the enumeration.
 */
BOOL CALLBACK CountResource(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    static_cast<void>(module);
    static_cast<void>(type);
    static_cast<void>(name);

    ++(*reinterpret_cast<std::size_t*>(param));
    return TRUE;
}

/**
 * @brief Count the version resources of an image.
 *
 * The shell shows the information of the first version resource of a file, so
 * a payload which was patched twice has to carry one of them only.
 *
 * @param[in] path Host path of the image.
 * @return The number of version resources, zero when the image cannot be read.
 */
std::size_t VersionResourceCount(const std::filesystem::path& path)
{
    const HMODULE module =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr)
    {
        return 0;
    }

    std::size_t count = 0;
    EnumResourceNamesW(module, RT_VERSION, CountResource, reinterpret_cast<LONG_PTR>(&count));
    FreeLibrary(module);
    return count;
}

/**
 * @brief Build a version information fixture.
 * @return The fixture, in the order the fields are read back in.
 */
appbox::ApplicationVersionInfo MakeVersionInfo()
{
    appbox::ApplicationVersionInfo info;
    info.translation.language = 0x0407;
    info.translation.code_page = 1200;
    info.fields = {
        { appbox::metadata_field::kFileDescription, L"My editor" },
        { appbox::metadata_field::kFileVersion,     L"1.2.3.4"   },
        { appbox::metadata_field::kProductName,     L"AppBox"    },
        { appbox::metadata_field::kProductVersion,  L"1.2.0.0"   },
        { appbox::metadata_field::kLegalCopyright,  L"(c) ACME"  },
    };
    return info;
}

} // namespace

TEST(Unit_ApplicationMetadata, ListsTheCommonFieldsFirst)
{
    const auto& all = appbox::MetadataFields();
    const auto& common = appbox::CommonMetadataFields();

    ASSERT_EQ(common.size(), appbox::kCommonMetadataFields);
    ASSERT_LE(common.size(), all.size());

    for (std::size_t index = 0; index < common.size(); ++index)
    {
        EXPECT_EQ(common[index], all[index]);
    }

    for (const auto& key : all)
    {
        EXPECT_TRUE(appbox::IsMetadataField(key)) << key;
        EXPECT_FALSE(appbox::MetadataFieldLabel(key).empty()) << key;
    }

    EXPECT_EQ(all.size(), static_cast<std::size_t>(12));
    EXPECT_FALSE(appbox::IsMetadataField("FileDescriptions"));
    EXPECT_TRUE(appbox::MetadataFieldLabel("FileDescriptions").empty());
}

TEST(Unit_ApplicationMetadata, StoresAndFindsFields)
{
    std::vector<appbox::MetadataField> fields;
    EXPECT_EQ(appbox::FindMetadataValue(fields, appbox::metadata_field::kFileDescription), nullptr);

    appbox::SetMetadataValue(fields, appbox::metadata_field::kFileDescription, L"Editor");
    appbox::SetMetadataValue(fields, appbox::metadata_field::kProductName, L"AppBox");
    ASSERT_EQ(fields.size(), static_cast<std::size_t>(2));

    const auto* description = appbox::FindMetadataValue(fields, appbox::metadata_field::kFileDescription);
    ASSERT_NE(description, nullptr);
    EXPECT_EQ(*description, L"Editor");

    /* Storing a key which the list holds replaces it in place. */
    appbox::SetMetadataValue(fields, appbox::metadata_field::kFileDescription, L"Writer");
    ASSERT_EQ(fields.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(fields[0].key, appbox::metadata_field::kFileDescription);
    EXPECT_EQ(fields[0].value, L"Writer");
    EXPECT_EQ(fields[1].key, appbox::metadata_field::kProductName);
}

TEST(Unit_ApplicationMetadata, MergesTheOverridesOverTheInheritedFields)
{
    const std::vector<appbox::MetadataField> inherited = {
        { appbox::metadata_field::kFileDescription, L"Source description" },
        { appbox::metadata_field::kFileVersion,     L"1.2.3.4"            },
    };
    const std::vector<appbox::MetadataField> overrides = {
        { appbox::metadata_field::kFileVersion, L"9.9.9.9" },
        { appbox::metadata_field::kProductName, L"Product" },
        { "NotAField",                          L"ignored" },
    };

    const auto merged = appbox::MergeMetadataFields(inherited, overrides);

    ASSERT_EQ(merged.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(merged[0].key, appbox::metadata_field::kFileDescription);
    EXPECT_EQ(merged[0].value, L"Source description");
    EXPECT_EQ(merged[1].key, appbox::metadata_field::kFileVersion);
    EXPECT_EQ(merged[1].value, L"9.9.9.9");
    EXPECT_EQ(merged[2].key, appbox::metadata_field::kProductName);
    EXPECT_EQ(merged[2].value, L"Product");
}

TEST(Unit_ApplicationMetadata, DiffsOnlyTheEditedFields)
{
    const std::vector<appbox::MetadataField> inherited = {
        { appbox::metadata_field::kFileDescription, L"Source description" },
        { appbox::metadata_field::kFileVersion,     L"1.2.3.4"            },
    };
    const std::vector<appbox::MetadataField> values = {
        { appbox::metadata_field::kFileDescription, L"Source description" }, /* Untouched. */
        { appbox::metadata_field::kFileVersion,     L""                   }, /* Emptied by the user. */
        { appbox::metadata_field::kProductName,     L""                   }, /* Never filled in. */
        { appbox::metadata_field::kCompanyName,     L"ACME"               }, /* Added by the user. */
    };

    const auto overrides = appbox::DiffMetadataFields(inherited, values);

    ASSERT_EQ(overrides.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(overrides[0].key, appbox::metadata_field::kFileVersion);
    EXPECT_TRUE(overrides[0].value.empty());
    EXPECT_EQ(overrides[1].key, appbox::metadata_field::kCompanyName);
    EXPECT_EQ(overrides[1].value, L"ACME");
}

TEST(Unit_ApplicationMetadata, DefaultsToTheFirstAutoStartProgram)
{
    TempDir temp;

    const auto folder = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(folder, L"tool.exe", "tool");
    MakeFile(folder, L"app.exe", "app");
    MakeFile(folder, L"zed.exe", "zed");
    MakeFile(folder, L"only.exe", "only");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", folder.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"tool.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"zed.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"only.exe", false, error)) << error;

    /* The executable name decides, not the order the files were added in. */
    EXPECT_EQ(appbox::DefaultMetadataSource(model), (folder / L"app.exe").wstring());
    EXPECT_EQ(appbox::MetadataSourcePath(model, appbox::ApplicationMetadata{}), (folder / L"app.exe").wstring());

    /* A session which names a program of its own uses that one. */
    appbox::ApplicationMetadata metadata;
    metadata.source = (folder / L"tool.exe").wstring();
    EXPECT_EQ(appbox::MetadataSourcePath(model, metadata), metadata.source);
    EXPECT_FALSE(metadata.IsEmpty());
}

TEST(Unit_ApplicationMetadata, HasNoDefaultSourceWithoutAnAutoStartProgram)
{
    TempDir temp;

    const auto folder = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(folder, L"app.exe", "app");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", folder.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", false, error)) << error;

    EXPECT_TRUE(appbox::DefaultMetadataSource(model).empty());
    EXPECT_TRUE(appbox::MetadataSourcePath(model, appbox::ApplicationMetadata{}).empty());
    EXPECT_TRUE(appbox::ApplicationMetadata{}.IsEmpty());
}

TEST(Unit_ApplicationMetadata, RejectsAnImageWithoutVersionInformation)
{
    appbox::ApplicationVersionInfo info;
    std::string                    error;

    /* The test executable is built without resources, so it carries none. */
    EXPECT_FALSE(appbox::ReadApplicationMetadata(SelfPath(), info, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(info.fields.empty());

    EXPECT_FALSE(appbox::ReadApplicationMetadata(L"", info, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_ApplicationMetadata, RoundTripsTheVersionInformation)
{
    TempDir temp;

    const auto image = temp.Get() / L"launcher.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), image.wstring()));

    const auto payload = ReadAllBytes(image.wstring());
    ASSERT_FALSE(payload.empty());

    const auto info = MakeVersionInfo();

    std::string warning;
    const auto  patched = appbox::ApplyApplicationMetadata(payload.data(), payload.size(), info, warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(patched.empty());
    ASSERT_TRUE(WriteAllBytes(image.wstring(), patched));

    /* The information is read back through the version API of Windows. */
    appbox::ApplicationVersionInfo read;
    std::string                    error;
    ASSERT_TRUE(appbox::ReadApplicationMetadata(image.wstring(), read, error)) << error;
    EXPECT_EQ(read.translation.language, info.translation.language);
    EXPECT_EQ(read.translation.code_page, info.translation.code_page);
    ASSERT_EQ(read.fields.size(), info.fields.size());
    for (std::size_t index = 0; index < info.fields.size(); ++index)
    {
        EXPECT_EQ(read.fields[index].key, info.fields[index].key);
        EXPECT_EQ(read.fields[index].value, info.fields[index].value);
    }

    EXPECT_EQ(VersionResourceCount(image), static_cast<std::size_t>(1));
}

TEST(Unit_ApplicationMetadata, ReplacesTheVersionResourceOfThePayload)
{
    TempDir temp;

    const auto image = temp.Get() / L"launcher.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), image.wstring()));

    const auto payload = ReadAllBytes(image.wstring());
    ASSERT_FALSE(payload.empty());

    std::string warning;
    const auto  first = appbox::ApplyApplicationMetadata(payload.data(), payload.size(), MakeVersionInfo(), warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(first.empty());
    ASSERT_TRUE(WriteAllBytes(image.wstring(), first));

    /* A second run patches an image which already carries a version resource. */
    appbox::ApplicationVersionInfo second_info;
    second_info.fields = {
        { appbox::metadata_field::kFileDescription, L"Second description" }
    };

    const auto second = appbox::ApplyApplicationMetadata(first.data(), first.size(), second_info, warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(second.empty());
    ASSERT_TRUE(WriteAllBytes(image.wstring(), second));

    appbox::ApplicationVersionInfo read;
    std::string                    error;
    ASSERT_TRUE(appbox::ReadApplicationMetadata(image.wstring(), read, error)) << error;
    ASSERT_EQ(read.fields.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(read.fields[0].key, appbox::metadata_field::kFileDescription);
    EXPECT_EQ(read.fields[0].value, L"Second description");

    /* The image never holds two version resources. */
    EXPECT_EQ(VersionResourceCount(image), static_cast<std::size_t>(1));
}

TEST(Unit_ApplicationMetadata, KeepsThePayloadWithoutAPeImage)
{
    appbox::ApplicationVersionInfo info;
    info.fields = {
        { appbox::metadata_field::kFileDescription, L"My editor" }
    };

    std::string warning;
    const auto  patched = appbox::ApplyApplicationMetadata("NOT-A-PE-IMAGE", 14, info, warning);

    EXPECT_TRUE(patched.empty());
    EXPECT_FALSE(warning.empty());
}

TEST(Unit_ApplicationMetadata, KeepsThePayloadWithoutVersionInformation)
{
    TempDir temp;

    const auto image = temp.Get() / L"launcher.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), image.wstring()));

    const auto payload = ReadAllBytes(image.wstring());
    ASSERT_FALSE(payload.empty());

    std::string warning;
    const auto  patched =
        appbox::ApplyApplicationMetadata(payload.data(), payload.size(), appbox::ApplicationVersionInfo{}, warning);

    EXPECT_TRUE(patched.empty());
    EXPECT_FALSE(warning.empty());
}

TEST(Unit_ApplicationMetadata, WritesTheVersionOfTheRealLauncherPayload)
{
    const auto launcher = appbox::test::LauncherPath();
    if (launcher.empty())
    {
        GTEST_SKIP() << "the launcher path was not passed with --launcher=<path>";
    }

    TempDir temp;

    const auto image = temp.Get() / L"launcher.exe";
    ASSERT_TRUE(CopyFileTo(launcher, image.wstring()));

    const auto payload = ReadAllBytes(image.wstring());
    ASSERT_FALSE(payload.empty());

    appbox::ApplicationVersionInfo info;
    info.fields = {
        { appbox::metadata_field::kFileDescription, L"Packaged application" },
        { appbox::metadata_field::kFileVersion,     L"2.5.0.1"              },
    };

    std::string warning;
    const auto  patched = appbox::ApplyApplicationMetadata(payload.data(), payload.size(), info, warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(patched.empty());
    ASSERT_TRUE(WriteAllBytes(image.wstring(), patched));

    appbox::ApplicationVersionInfo read;
    std::string                    error;
    ASSERT_TRUE(appbox::ReadApplicationMetadata(image.wstring(), read, error)) << error;
    ASSERT_EQ(read.fields.size(), info.fields.size());
    EXPECT_EQ(read.fields[0].value, L"Packaged application");
    EXPECT_EQ(read.fields[1].value, L"2.5.0.1");

    /* The real launcher carries no version resource of its own. */
    EXPECT_EQ(VersionResourceCount(image), static_cast<std::size_t>(1));
}
