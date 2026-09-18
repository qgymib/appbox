#include <gtest/gtest.h>
#include "src/core/PackService.hpp"
#include "WString.hpp"
#include "Config.hpp"
#include <nlohmann/json.hpp>
#include <zip.h>
#include <chrono>
#include <filesystem>
#include <set>
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
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-packservice-" + UniqueFragment());
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
 * @brief Create a file with content below a parent directory.
 * @param[in] parent Parent directory.
 * @param[in] relative Relative file path.
 * @param[in] content File content.
 * @return The created file path.
 */
std::filesystem::path MakeFile(const std::filesystem::path& parent, const std::wstring& relative,
                               const std::string& content)
{
    const auto file = parent / relative;
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
 * @brief Read the content of one zip entry.
 * @param[in] archive Open zip archive.
 * @param[in] name Entry name.
 * @return The entry content, empty when the entry cannot be read.
 */
std::string ReadEntry(zip_t* archive, const char* name)
{
    const auto index = zip_name_locate(archive, name, 0);
    if (index < 0)
    {
        return {};
    }

    zip_stat_t stat = {};
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) < 0)
    {
        return {};
    }

    std::string content(stat.size, '\0');
    zip_file_t* file = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
    if (file == nullptr)
    {
        return {};
    }
    const auto read = zip_fread(file, content.data(), stat.size);
    zip_fclose(file);
    if (read != static_cast<zip_int64_t>(stat.size))
    {
        return {};
    }
    return content;
}

/**
 * @brief Open a zip archive for reading.
 * @param[in] path Archive path.
 * @return The archive, nullptr on failure.
 */
zip_t* OpenArchive(const std::wstring& path)
{
    int error_code = 0;
    return zip_open(appbox::WideToUTF8(path).c_str(), ZIP_RDONLY, &error_code);
}

/**
 * @brief RAII helper closing a zip archive.
 */
class ZipArchiveCloser
{
public:
    /**
     * @brief Remember the archive to close.
     * @param[in] archive Archive handle, may be nullptr.
     */
    explicit ZipArchiveCloser(zip_t* handle)
        : archive(handle)
    {
    }

    ~ZipArchiveCloser()
    {
        if (archive != nullptr)
        {
            zip_close(archive);
        }
    }

    /**
     * @brief The wrapped archive handle.
     */
    zip_t* archive = nullptr;
};

} // namespace

TEST(PackService, CountFilesBelowCountsRecursively)
{
    TempDir temp;
    MakeFile(temp.Get(), L"a.txt", "A");
    MakeFile(temp.Get(), L"sub\\b.txt", "B");
    MakeFile(temp.Get(), L"sub\\deep\\c.txt", "C");
    std::filesystem::create_directories(temp.Get() / L"sub" / L"empty");

    EXPECT_EQ(appbox::CountFilesBelow(temp.Get().wstring()), static_cast<std::size_t>(3));
}

TEST(PackService, PackRequiresAMainProgram)
{
    TempDir temp;
    appbox::PackModel model;

    const auto result = appbox::Pack(model, "LOADER", 6, (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("main program"), std::string::npos);
}

TEST(PackService, PackRequiresLoaderBytes)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", temp.Get().filename().wstring(), L"app.exe",
                                     error))
        << error;

    const auto result = appbox::Pack(model, nullptr, 0, (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("loader payload"), std::string::npos);
}

TEST(PackService, PackProducesLoaderConfigurationAndLayers)
{
    TempDir program_files;
    TempDir user_profile;
    const auto my_app = program_files.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE-CONTENT");
    MakeFile(my_app, L"data\\config.txt", "CFG-CONTENT");
    std::filesystem::create_directories(my_app / L"emptydir");
    MakeFile(user_profile.Get(), L"MyUser\\settings.ini", "INI-CONTENT");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("user_profile", (user_profile.Get() / L"MyUser").wstring(),
                                   error))
        << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;

    const auto zip_path = program_files.Get().parent_path()
        / (program_files.Get().filename().wstring() + L"-pack.zip");
    const auto result = appbox::Pack(model, "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t* archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* The loader payload is embedded under the name of the main program. */
    EXPECT_EQ(appbox::LoaderEntryName(model), L"app.exe");
    EXPECT_EQ(ReadEntry(archive, "app.exe"), "FAKE-LOADER");

    /* The archive carries a single naming, the loader name is gone. */
    EXPECT_EQ(zip_name_locate(archive, "AppBoxLoader.exe", 0), -1);
    EXPECT_EQ(zip_name_locate(archive, "AppBoxLoader.json", 0), -1);

    /* The configuration matches the loader runtime conventions. */
    const auto json_text = ReadEntry(archive, "app.exe.json");
    ASSERT_FALSE(json_text.empty());
    const auto config = nlohmann::json::parse(json_text).get<appbox::LoaderConfig>();
    ASSERT_EQ(config.base_fs.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(config.base_fs[0], ".");
    EXPECT_EQ(config.overlay_fs, "data");
    EXPECT_EQ(config.launch.executable, "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_TRUE(config.launch.arguments.empty());

    /* Imported folders become lower layers below filesystem/<layer key>. */
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/data/config.txt"), "CFG-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "filesystem/#USERPROFILE#/MyUser/settings.ini"), "INI-CONTENT");

    /* Empty folders survive as directory entries (with trailing slash). */
    const auto empty_index =
        zip_name_locate(archive, "filesystem/#ProgramFiles#/MyApp/emptydir/", 0);
    EXPECT_GE(empty_index, 0);
}

TEST(PackService, LoaderEntryNameIsEmptyWithoutAMainProgram)
{
    appbox::PackModel model;

    EXPECT_TRUE(appbox::LoaderEntryName(model).empty());
}

TEST(PackService, LoaderEntryNameDropsTheDirectoryOfTheMainProgram)
{
    TempDir     temp;
    const auto  my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"bin\\tool.exe", "EXE");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"bin\\tool.exe", error)) << error;

    /* Only the file name is used: the loader lives in the archive root. */
    EXPECT_EQ(appbox::LoaderEntryName(model), L"tool.exe");

    const auto zip_path =
        temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-entry.zip");
    const auto result = appbox::Pack(model, "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    EXPECT_EQ(ReadEntry(archive, "tool.exe"), "FAKE-LOADER");

    const auto json_text = ReadEntry(archive, "tool.exe.json");
    ASSERT_FALSE(json_text.empty());
    const auto config = nlohmann::json::parse(json_text).get<appbox::LoaderConfig>();
    EXPECT_EQ(config.launch.executable, "#ProgramFiles#\\MyApp\\bin\\tool.exe");

    /* The entry program itself keeps its place below the layer tree. */
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/bin/tool.exe"), "EXE");
}

TEST(PackService, PackReportsProgress)
{
    TempDir temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFile(my_app, L"data.txt", "DATA");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;

    std::vector<appbox::BuildProgress> reports;
    const auto progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto zip_path = temp.Get().parent_path()
        / (temp.Get().filename().wstring() + L"-progress.zip");
    const auto result = appbox::Pack(model, "FAKE", 4, zip_path.wstring(), progress);
    EXPECT_EQ(result, "") << result;

    ASSERT_FALSE(reports.empty());

    /* The run opens with the preparing stage, which has no file of its own. */
    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Preparing);
    EXPECT_EQ(reports.front().done, static_cast<std::size_t>(0));
    EXPECT_EQ(reports.front().total, static_cast<std::size_t>(2));
    EXPECT_TRUE(reports.front().current.empty());

    /* It closes with the complete count of the packing stage. */
    EXPECT_EQ(reports.back().stage, appbox::BuildStage::Packing);
    EXPECT_EQ(reports.back().done, static_cast<std::size_t>(2));
    EXPECT_EQ(reports.back().total, static_cast<std::size_t>(2));

    /* Every packed file is named by its path below the import root. */
    std::set<std::wstring> named;
    for (const auto& report : reports)
    {
        if (report.stage != appbox::BuildStage::Packing)
        {
            continue;
        }
        if (!report.current.empty())
        {
            named.insert(report.current);
        }
    }
    EXPECT_EQ(named.count(L"MyApp\\app.exe"), static_cast<std::size_t>(1));
    EXPECT_EQ(named.count(L"MyApp\\data.txt"), static_cast<std::size_t>(1));
}

TEST(PackService, PackCanBeCancelled)
{
    TempDir temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;

    const auto zip_path = temp.Get().parent_path()
        / (temp.Get().filename().wstring() + L"-cancel.zip");
    const auto result = appbox::Pack(model, "FAKE", 4, zip_path.wstring(),
                                     [](const appbox::BuildProgress&) { return false; });
    EXPECT_EQ(result, appbox::kBuildCancelledError);
}

TEST(PackService, PackWritesImportedFiles)
{
    TempDir temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFolder(my_app, L"data");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA-CONTENT");
    const auto note = MakeFile(temp.Get(), L"note.txt", "NOTE-CONTENT");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\data", { note.wstring() }, error))
        << error;

    const auto zip_path = temp.Get().parent_path()
        / (temp.Get().filename().wstring() + L"-files.zip");
    const auto result = appbox::Pack(model, "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t* archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* Imported files share the layer tree of the imported folder. */
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/extra.dll"), "EXTRA-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/data/note.txt"), "NOTE-CONTENT");

    /* The imported folder content is untouched. */
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE");
}

TEST(PackService, PackCreatesDirectoriesOfImportedFiles)
{
    TempDir temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;

    /* The intermediate folder exists on the host. */
    MakeFolder(my_app, L"plugins");
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\plugins\\deep", { extra.wstring() },
                                  error))
        << error;

    const auto zip_path = temp.Get().parent_path()
        / (temp.Get().filename().wstring() + L"-dirs.zip");
    const auto result = appbox::Pack(model, "FAKE", 4, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t* archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* Every missing prefix of the target directory becomes a directory entry. */
    EXPECT_GE(zip_name_locate(archive, "filesystem/#ProgramFiles#/MyApp/plugins/", 0), 0);
    EXPECT_GE(zip_name_locate(archive, "filesystem/#ProgramFiles#/MyApp/plugins/deep/", 0), 0);
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/plugins/deep/extra.dll"), "EXTRA");
}

TEST(PackService, PackCountsImportedFilesInTheProgressTotal)
{
    TempDir temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFile(my_app, L"data.txt", "DATA");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;

    std::vector<appbox::BuildProgress> reports;
    const auto progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto zip_path = temp.Get().parent_path()
        / (temp.Get().filename().wstring() + L"-files-progress.zip");
    const auto result = appbox::Pack(model, "FAKE", 4, zip_path.wstring(), progress);
    EXPECT_EQ(result, "") << result;

    ASSERT_FALSE(reports.empty());
    /* Two files of the imported folder plus one imported file. */
    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Preparing);
    EXPECT_EQ(reports.front().done, static_cast<std::size_t>(0));
    EXPECT_EQ(reports.front().total, static_cast<std::size_t>(3));
    EXPECT_EQ(reports.back().stage, appbox::BuildStage::Packing);
    EXPECT_EQ(reports.back().done, static_cast<std::size_t>(3));
    EXPECT_EQ(reports.back().total, static_cast<std::size_t>(3));

    /* An imported file is named by its target directory and its file name. */
    std::set<std::wstring> named;
    for (const auto& report : reports)
    {
        if (report.stage == appbox::BuildStage::Packing && !report.current.empty())
        {
            named.insert(report.current);
        }
    }
    EXPECT_EQ(named.count(L"MyApp\\extra.dll"), static_cast<std::size_t>(1));
}
