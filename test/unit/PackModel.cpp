#include <gtest/gtest.h>
#include "src/core/PackModel.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

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
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-packmodel-" + UniqueFragment());
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

} // namespace

TEST(Unit_PresetDirectory, ProvidesExpectedPresets)
{
    const auto& presets = appbox::PresetDirectories();
    ASSERT_EQ(presets.size(), static_cast<std::size_t>(4));

    EXPECT_EQ(presets[0].id, "program_files");
    EXPECT_EQ(presets[0].parent_id, "");
    EXPECT_EQ(presets[0].layer_key, L"#ProgramFiles#");
    EXPECT_FALSE(presets[0].display_name.empty());
    EXPECT_TRUE(std::filesystem::path(presets[0].real_path).is_absolute());

    EXPECT_EQ(presets[1].id, "user_profile");
    EXPECT_EQ(presets[1].parent_id, "");
    EXPECT_EQ(presets[1].layer_key, L"#USERPROFILE#");
    EXPECT_TRUE(std::filesystem::path(presets[1].real_path).is_absolute());

    /* The folders of the user profile hang below the profile itself. */
    EXPECT_EQ(presets[2].id, "documents");
    EXPECT_EQ(presets[2].parent_id, "user_profile");
    EXPECT_EQ(presets[2].display_name, L"Documents");
    EXPECT_EQ(presets[2].layer_key, L"#Documents#");
    EXPECT_TRUE(std::filesystem::path(presets[2].real_path).is_absolute());

    EXPECT_EQ(presets[3].id, "desktop");
    EXPECT_EQ(presets[3].parent_id, "user_profile");
    EXPECT_EQ(presets[3].display_name, L"Desktop");
    EXPECT_EQ(presets[3].layer_key, L"#Desktop#");
    EXPECT_TRUE(std::filesystem::path(presets[3].real_path).is_absolute());

    /* Every preset owns a layer of its own, so the keys are unique. */
    for (std::size_t i = 0; i < presets.size(); ++i)
    {
        for (std::size_t j = i + 1; j < presets.size(); ++j)
        {
            EXPECT_NE(presets[i].layer_key, presets[j].layer_key);
            EXPECT_NE(presets[i].id, presets[j].id);
        }
    }
}

TEST(Unit_PresetDirectory, ChildPresetsNestTheUserFolders)
{
    const auto top = appbox::ChildPresets("");
    ASSERT_EQ(top.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(top[0].id, "program_files");
    EXPECT_EQ(top[1].id, "user_profile");

    const auto nested = appbox::ChildPresets("user_profile");
    ASSERT_EQ(nested.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(nested[0].id, "documents");
    EXPECT_EQ(nested[0].layer_key, L"#Documents#");
    EXPECT_EQ(nested[1].id, "desktop");
    EXPECT_EQ(nested[1].layer_key, L"#Desktop#");

    /* A preset without nested presets and an unknown preset hold nothing. */
    EXPECT_TRUE(appbox::ChildPresets("program_files").empty());
    EXPECT_TRUE(appbox::ChildPresets("documents").empty());
    EXPECT_TRUE(appbox::ChildPresets("does_not_exist").empty());

    /*
     * The tree of the presets holds every preset exactly once, which is what
     * the filesystem workspace walks to build its items.
     */
    std::size_t reachable = top.size();
    for (const auto& preset : top)
    {
        reachable += appbox::ChildPresets(preset.id).size();
    }
    EXPECT_EQ(reachable, appbox::PresetDirectories().size());
}

TEST(Unit_PresetDirectory, FindsKnownAndRejectsUnknownIds)
{
    appbox::PresetDirectory preset;
    EXPECT_TRUE(appbox::FindPresetDirectory("program_files", preset));
    EXPECT_EQ(preset.id, "program_files");
    EXPECT_TRUE(appbox::FindPresetDirectory("user_profile", preset));
    EXPECT_EQ(preset.id, "user_profile");
    EXPECT_TRUE(appbox::FindPresetDirectory("documents", preset));
    EXPECT_EQ(preset.layer_key, L"#Documents#");
    EXPECT_TRUE(appbox::FindPresetDirectory("desktop", preset));
    EXPECT_EQ(preset.layer_key, L"#Desktop#");
    EXPECT_FALSE(appbox::FindPresetDirectory("does_not_exist", preset));
}

TEST(Unit_PresetDirectory, ContainerLabelNamesTheFilesystemTreeRoot)
{
    /*
     * The label is the top item of the filesystem tree of the packer, so it is
     * user visible text which must not drift apart from the container of the
     * registry view.
     */
    EXPECT_STREQ(appbox::kFilesystemContainerLabel, L"Sandbox Filesystem");
    EXPECT_STRNE(appbox::kFilesystemContainerLabel, L"Sandbox Registry");

    for (const auto& preset : appbox::PresetDirectories())
    {
        EXPECT_STRNE(appbox::kFilesystemContainerLabel, preset.display_name.c_str());
    }
}

TEST(Unit_PackModel, ImportFolderAcceptsValidFolder)
{
    TempDir temp;

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;

    const auto imports = model.ImportsOf("program_files");
    ASSERT_EQ(imports.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(imports[0].import_name, temp.Get().filename().wstring());
    EXPECT_EQ(imports[0].source_path, temp.Get().wstring());
    EXPECT_EQ(model.ImportsOf("user_profile").size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, ImportFolderRejectsUnknownPreset)
{
    TempDir temp;

    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.ImportFolder("does_not_exist", temp.Get().wstring(), error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_PackModel, ImportFolderRejectsMissingFolder)
{
    appbox::PackModel model;
    std::string       error;
    const auto        missing = std::filesystem::temp_directory_path() / L"appbox-no-such-folder";
    EXPECT_FALSE(model.ImportFolder("program_files", missing.wstring(), error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_PackModel, ImportFolderRejectsDuplicateName)
{
    TempDir    parent1;
    TempDir    parent2;
    const auto folder1 = MakeFolder(parent1.Get(), L"MyApp");
    const auto folder2 = MakeFolder(parent2.Get(), L"MyApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", folder1.wstring(), error)) << error;
    EXPECT_FALSE(model.ImportFolder("program_files", folder2.wstring(), error));
    EXPECT_NE(error.find("MyApp"), std::string::npos);
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, ImportFolderAllowsSameNameInDifferentPresets)
{
    TempDir    parent1;
    TempDir    parent2;
    const auto folder1 = MakeFolder(parent1.Get(), L"MyApp");
    const auto folder2 = MakeFolder(parent2.Get(), L"MyApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", folder1.wstring(), error)) << error;
    EXPECT_TRUE(model.ImportFolder("user_profile", folder2.wstring(), error)) << error;
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.ImportsOf("user_profile").size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, AddStartupFileAcceptsExecutable)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"app.exe", true, error)) << error;

    EXPECT_TRUE(model.HasStartupFiles());
    EXPECT_TRUE(model.HasAutoStart());
    EXPECT_TRUE(model.IsStartupFile("program_files", import_name, L"app.exe"));

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].preset_id, "program_files");
    EXPECT_EQ(files[0].import_name, import_name);
    EXPECT_EQ(files[0].relative_path, L"app.exe");
    EXPECT_EQ(files[0].trigger, L"app");
    EXPECT_TRUE(files[0].auto_start);
}

TEST(Unit_PackModel, AddStartupFileNormalizesSeparators)
{
    TempDir    temp;
    const auto bin = MakeFolder(temp.Get(), L"bin");
    MakeFile(bin, L"app.exe", "EXE");
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_TRUE(model.AddStartupFile("program_files", import_name, L"/bin//app.exe", true, error)) << error;

    ASSERT_EQ(model.StartupFiles().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.StartupFiles()[0].relative_path, L"bin\\app.exe");
}

TEST(Unit_PackModel, AddStartupFileRejectsNonExecutable)
{
    TempDir temp;
    MakeFile(temp.Get(), L"readme.txt", "TXT");
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_FALSE(model.AddStartupFile("program_files", import_name, L"readme.txt", true, error));
    EXPECT_NE(error.find(".exe"), std::string::npos);
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, AddStartupFileRejectsMissingFile)
{
    TempDir    temp;
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_FALSE(model.AddStartupFile("program_files", import_name, L"missing.exe", true, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, AddStartupFileRejectsEscapingPath)
{
    TempDir    temp;
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_FALSE(model.AddStartupFile("program_files", import_name, L"..\\outside.exe", true, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_PackModel, AddStartupFileRejectsUnknownImport)
{
    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.AddStartupFile("program_files", L"Missing", L"app.exe", true, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, AddStartupFileDerivesUniqueTriggers)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(MakeFolder(app, L"bin"), L"app.exe", "EXE");
    MakeFile(MakeFolder(app, L"tool"), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"bin\\app.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"tool\\app.exe", true, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(files[0].trigger, L"app");
    EXPECT_EQ(files[1].trigger, L"app-2");
}

TEST(Unit_PackModel, AddStartupFileUpdatesTheFlagOfAnExistingFile)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"app.exe", "EXE");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", false, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_FALSE(files[0].auto_start);
    EXPECT_EQ(files[0].trigger, L"app");
    EXPECT_FALSE(model.HasAutoStart());
}

TEST(Unit_PackModel, RemoveImportDropsReferencedStartupFiles)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"app.exe", true, error)) << error;
    EXPECT_TRUE(model.HasStartupFiles());

    model.RemoveImport("program_files", import_name);
    EXPECT_FALSE(model.HasStartupFiles());
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, RemoveImportKeepsUnrelatedStartupFiles)
{
    TempDir temp1;
    TempDir temp2;
    MakeFile(temp1.Get(), L"one.exe", "ONE");
    MakeFile(temp2.Get(), L"two.exe", "TWO");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp1.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("user_profile", temp2.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", temp1.Get().filename().wstring(), L"one.exe", true, error))
        << error;

    model.RemoveImport("user_profile", temp2.Get().filename().wstring());
    EXPECT_TRUE(model.HasStartupFiles());
    EXPECT_EQ(model.ImportsOf("user_profile").size(), static_cast<std::size_t>(0));
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, RemoveImportedFileDropsReferencedStartupFile)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"tool.exe", true, error)) << error;

    EXPECT_TRUE(model.RemoveImportedFile("program_files", L"MyApp", L"tool.exe"));
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, StartupFilePathResolvesHostFile)
{
    TempDir    temp;
    const auto exe = MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", temp.Get().filename().wstring(), L"app.exe", true, error))
        << error;

    std::wstring path;
    EXPECT_TRUE(model.StartupFilePath(model.StartupFiles().front(), path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(), exe.lexically_normal());
}

TEST(Unit_PackModel, SetStartupFilesReplacesTheList)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"one.exe", "ONE");
    MakeFile(app, L"two.exe", "TWO");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"one.exe", true, error)) << error;

    appbox::StartupFile first;
    first.preset_id = "program_files";
    first.import_name = L"myapp";
    first.relative_path = L"two.exe";
    first.trigger = L" second ";
    first.auto_start = false;

    ASSERT_TRUE(model.SetStartupFiles({ first }, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].import_name, L"MyApp");
    EXPECT_EQ(files[0].relative_path, L"two.exe");
    EXPECT_EQ(files[0].trigger, L"second");
    EXPECT_FALSE(files[0].auto_start);
}

TEST(Unit_PackModel, SetStartupFilesRejectsDuplicateTrigger)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"one.exe", "ONE");
    MakeFile(app, L"two.exe", "TWO");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    appbox::StartupFile first;
    first.preset_id = "program_files";
    first.import_name = L"MyApp";
    first.relative_path = L"one.exe";
    first.trigger = L"app";

    appbox::StartupFile second = first;
    second.relative_path = L"two.exe";
    second.trigger = L"APP";

    EXPECT_FALSE(model.SetStartupFiles({ first, second }, error));
    EXPECT_NE(error.find("used twice"), std::string::npos);
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, SetStartupFilesRejectsEmptyTrigger)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"one.exe", "ONE");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    appbox::StartupFile file;
    file.preset_id = "program_files";
    file.import_name = L"MyApp";
    file.relative_path = L"one.exe";
    file.trigger = L"   ";

    EXPECT_FALSE(model.SetStartupFiles({ file }, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, SetStartupFilesKeepsTheListOnFailure)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"one.exe", "ONE");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"one.exe", true, error)) << error;

    appbox::StartupFile broken;
    broken.preset_id = "program_files";
    broken.import_name = L"MyApp";
    broken.relative_path = L"missing.exe";
    broken.trigger = L"broken";

    EXPECT_FALSE(model.SetStartupFiles({ broken }, error));
    ASSERT_EQ(model.StartupFiles().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.StartupFiles()[0].relative_path, L"one.exe");
}

TEST(Unit_PackModel, ImportFilesAcceptsFileInsideImportedFolder)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error)) << error;

    const auto files = model.FilesOf("program_files", L"MyApp");
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].preset_id, "program_files");
    EXPECT_EQ(files[0].target_dir, L"MyApp");
    EXPECT_EQ(files[0].file_name, L"tool.exe");
    EXPECT_EQ(files[0].source_path, tool.wstring());
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.FilesOf("user_profile", L"MyApp").size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, ImportFilesNormalizesNestedTargetDirectory)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFolder(app, L"data");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"/MyApp/data/", { tool.wstring() }, error)) << error;

    const auto files = model.FilesOf("program_files", L"MyApp\\data");
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].target_dir, L"MyApp\\data");
}

TEST(Unit_PackModel, ImportFilesKeepsSelectionOrder)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto first = MakeFile(temp.Get(), L"first.dll", "1");
    const auto second = MakeFile(temp.Get(), L"second.dll", "2");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { second.wstring(), first.wstring() }, error)) << error;

    const auto files = model.AllImportedFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(files[0].file_name, L"second.dll");
    EXPECT_EQ(files[1].file_name, L"first.dll");
}

TEST(Unit_PackModel, ImportFilesRejectsUnknownPreset)
{
    TempDir    temp;
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.ImportFiles("does_not_exist", L"MyApp", { tool.wstring() }, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_PackModel, ImportFilesRejectsTargetOutsideAnImportedFolder)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"Other", { tool.wstring() }, error));
    EXPECT_NE(error.find("not an imported folder"), std::string::npos);
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, ImportFilesRejectsEscapingTargetDirectory)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp\\..\\..", { tool.wstring() }, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, ImportFilesRejectsMissingSource)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto missing = temp.Get() / L"missing.dll";

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { missing.wstring() }, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_PackModel, ImportFilesRejectsEmptySelection)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", {}, error));
    EXPECT_FALSE(error.empty());
}

TEST(Unit_PackModel, ImportFilesRejectsDuplicateImport)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, ImportFilesRejectsNameAlreadyInTheImportedFolder)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"tool.exe", "HOST");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error));
    EXPECT_NE(error.find("already exists in the imported folder"), std::string::npos);
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, ImportFilesIsAtomic)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto good = MakeFile(temp.Get(), L"good.dll", "GOOD");
    const auto missing = temp.Get() / L"missing.dll";

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { good.wstring(), missing.wstring() }, error));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, RemoveImportedFileRemovesTheEntry)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error)) << error;

    EXPECT_TRUE(model.RemoveImportedFile("program_files", L"MyApp", L"tool.exe"));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
    EXPECT_FALSE(model.RemoveImportedFile("program_files", L"MyApp", L"tool.exe"));
}

TEST(Unit_PackModel, RemoveImportCascadesImportedFiles)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto other = MakeFolder(temp.Get(), L"OtherApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("program_files", other.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\data", { tool.wstring() }, error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"OtherApp", { tool.wstring() }, error)) << error;
    ASSERT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(2));

    model.RemoveImport("program_files", L"MyApp");

    const auto remaining = model.AllImportedFiles();
    ASSERT_EQ(remaining.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(remaining[0].target_dir, L"OtherApp");
}

TEST(Unit_PackModel, ClearResetsEveryPartOfTheModel)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"tool.exe", "TOOL");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel model;
    EXPECT_TRUE(model.IsEmpty());

    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"tool.exe", true, error)) << error;
    EXPECT_FALSE(model.IsEmpty());

    model.Clear();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(0));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, RestoreImportedFolderAcceptsMissingSourceFolder)
{
    const auto missing = std::filesystem::temp_directory_path() / L"appbox-no-such-restored-folder";

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", missing.wstring(), error)) << error;

    const auto imports = model.ImportsOf("program_files");
    ASSERT_EQ(imports.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(imports[0].import_name, L"MyApp");
    EXPECT_EQ(imports[0].source_path, missing.wstring());
    EXPECT_FALSE(model.IsEmpty());
}

TEST(Unit_PackModel, RestoreImportedFolderRejectsUnknownPreset)
{
    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.RestoreImportedFolder("does_not_exist", L"MyApp", L"C:\\MyApp", error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_PackModel, RestoreImportedFolderRejectsDuplicateNameIgnoringCase)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;

    EXPECT_FALSE(model.RestoreImportedFolder("program_files", L"MYAPP", L"C:\\Other", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, RestoreImportedFolderRejectsUnusableName)
{
    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.RestoreImportedFolder("program_files", L"..", L"C:\\MyApp", error));
    EXPECT_FALSE(model.RestoreImportedFolder("program_files", L"MyApp\\data", L"C:\\MyApp", error));
    EXPECT_FALSE(model.RestoreImportedFolder("program_files", L"MyApp", L"", error));
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_PackModel, RestoreImportedFileAcceptsMissingSourceFile)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;
    ASSERT_TRUE(model.RestoreImportedFile("program_files", L"myapp\\data", L"tool.exe", L"C:\\tmp\\tool.exe", error))
        << error;

    const auto files = model.AllImportedFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].preset_id, "program_files");
    /* The stored directory uses the canonical spelling of the imported folder. */
    EXPECT_EQ(files[0].target_dir, L"MyApp\\data");
    EXPECT_EQ(files[0].file_name, L"tool.exe");
    EXPECT_EQ(files[0].source_path, L"C:\\tmp\\tool.exe");
}

TEST(Unit_PackModel, RestoreImportedFileRejectsTargetOutsideAnImportedFolder)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;

    EXPECT_FALSE(model.RestoreImportedFile("program_files", L"Other", L"tool.exe", L"C:\\tool.exe", error));
    EXPECT_NE(error.find("not an imported folder"), std::string::npos);
    EXPECT_FALSE(model.RestoreImportedFile("program_files", L"MyApp\\..", L"tool.exe", L"C:\\tool.exe", error));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, RestoreImportedFileRejectsDuplicateEntry)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;
    ASSERT_TRUE(model.RestoreImportedFile("program_files", L"MyApp", L"tool.exe", L"C:\\tool.exe", error)) << error;

    EXPECT_FALSE(model.RestoreImportedFile("program_files", L"MyApp", L"TOOL.EXE", L"C:\\other.exe", error));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, RestoreStartupFileAcceptsMissingExecutable)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;
    ASSERT_TRUE(model.RestoreStartupFile("program_files", L"myapp", L"bin/app.exe", L" tool ", true, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].import_name, L"MyApp");
    EXPECT_EQ(files[0].relative_path, L"bin\\app.exe");
    EXPECT_EQ(files[0].trigger, L"tool");
    EXPECT_TRUE(files[0].auto_start);

    std::wstring path;
    EXPECT_TRUE(model.StartupFilePath(files[0], path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(),
              std::filesystem::path(L"C:\\MyApp\\bin\\app.exe").lexically_normal());
}

TEST(Unit_PackModel, RestoreStartupFileRejectsInvalidSelection)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;

    EXPECT_FALSE(model.RestoreStartupFile("program_files", L"Missing", L"app.exe", L"app", true, error));
    EXPECT_FALSE(model.RestoreStartupFile("program_files", L"MyApp", L"", L"app", true, error));
    EXPECT_FALSE(model.RestoreStartupFile("program_files", L"MyApp", L"..\\app.exe", L"app", true, error));
    EXPECT_FALSE(model.RestoreStartupFile("program_files", L"MyApp", L"readme.txt", L"app", true, error));
    EXPECT_NE(error.find(".exe"), std::string::npos);
    EXPECT_FALSE(model.RestoreStartupFile("program_files", L"MyApp", L"app.exe", L"  ", true, error));
    EXPECT_FALSE(model.HasStartupFiles());
}

TEST(Unit_PackModel, RestoreStartupFileRejectsDuplicateTrigger)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;
    ASSERT_TRUE(model.RestoreStartupFile("program_files", L"MyApp", L"one.exe", L"app", true, error)) << error;

    EXPECT_FALSE(model.RestoreStartupFile("program_files", L"MyApp", L"two.exe", L"APP", true, error));
    EXPECT_NE(error.find("used twice"), std::string::npos);
    EXPECT_EQ(model.StartupFiles().size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, HostFolderPathResolvesThePresetDirectory)
{
    appbox::PresetDirectory preset;
    ASSERT_TRUE(appbox::FindPresetDirectory("program_files", preset));

    /* A preset node carries no import name, so it maps to the layer root. */
    appbox::PackModel model;
    std::wstring      path;
    EXPECT_TRUE(model.HostFolderPath("program_files", std::wstring(), std::wstring(), path));
    EXPECT_EQ(path, preset.real_path);
}

TEST(Unit_PackModel, HostFolderPathResolvesTheImportRoot)
{
    TempDir temp;

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("user_profile", temp.Get().wstring(), error)) << error;

    /* An import root maps to the host folder it was imported from. */
    std::wstring path;
    EXPECT_TRUE(model.HostFolderPath("user_profile", temp.Get().filename().wstring(), std::wstring(), path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(), temp.Get().lexically_normal());
}

TEST(Unit_PackModel, HostFolderPathAppendsTheRelativeDirectory)
{
    TempDir    temp;
    const auto sub = MakeFolder(temp.Get(), L"bin");
    MakeFolder(sub, L"x64");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;

    const auto import_name = temp.Get().filename().wstring();

    std::wstring path;
    EXPECT_TRUE(model.HostFolderPath("program_files", import_name, L"bin", path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(), sub.lexically_normal());

    EXPECT_TRUE(model.HostFolderPath("program_files", import_name, L"bin\\x64", path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(), (sub / L"x64").lexically_normal());
}

TEST(Unit_PackModel, HostFolderPathDoesNotTouchTheHostFilesystem)
{
    /*
     * A project file can be imported on a machine where the packaged
     * application is not installed, so the mapping answers the path of a
     * folder which does not exist as well. The import name is matched ignoring
     * the case, like every other lookup of the model.
     */
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:/Host/MyApp", error)) << error;

    std::wstring path;
    EXPECT_TRUE(model.HostFolderPath("program_files", L"MyApp", std::wstring(), path));
    EXPECT_EQ(path, L"C:/Host/MyApp");

    EXPECT_TRUE(model.HostFolderPath("program_files", L"myapp", L"bin", path));
    EXPECT_EQ(path, (std::filesystem::path(L"C:/Host/MyApp") / L"bin").wstring());
}

TEST(Unit_PackModel, HostFolderPathRejectsTheContainer)
{
    /*
     * The container of the filesystem view holds the preset directories and is
     * not a folder of the model, so it must not answer a path.
     */
    appbox::PackModel model;

    std::wstring path = L"untouched";
    EXPECT_FALSE(model.HostFolderPath(std::string(), std::wstring(), std::wstring(), path));
    EXPECT_EQ(path, L"untouched");
}

TEST(Unit_PackModel, HostFolderPathRejectsUnknownPresetAndImport)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:/Host/MyApp", error)) << error;

    std::wstring path = L"untouched";
    EXPECT_FALSE(model.HostFolderPath("does_not_exist", std::wstring(), std::wstring(), path));
    EXPECT_EQ(path, L"untouched");

    /* The preset exists, but the folder below it is not imported. */
    EXPECT_FALSE(model.HostFolderPath("program_files", L"Missing", L"bin", path));
    EXPECT_EQ(path, L"untouched");
}
