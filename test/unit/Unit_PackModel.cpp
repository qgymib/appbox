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

TEST(PresetDirectory, ProvidesExpectedPresets)
{
    const auto& presets = appbox::PresetDirectories();
    ASSERT_EQ(presets.size(), static_cast<std::size_t>(2));

    EXPECT_EQ(presets[0].id, "program_files");
    EXPECT_EQ(presets[0].layer_key, L"#ProgramFiles#");
    EXPECT_FALSE(presets[0].display_name.empty());
    EXPECT_TRUE(std::filesystem::path(presets[0].real_path).is_absolute());

    EXPECT_EQ(presets[1].id, "user_profile");
    EXPECT_EQ(presets[1].layer_key, L"#USERPROFILE#");
    EXPECT_TRUE(std::filesystem::path(presets[1].real_path).is_absolute());
}

TEST(PresetDirectory, FindsKnownAndRejectsUnknownIds)
{
    appbox::PresetDirectory preset;
    EXPECT_TRUE(appbox::FindPresetDirectory("program_files", preset));
    EXPECT_EQ(preset.id, "program_files");
    EXPECT_TRUE(appbox::FindPresetDirectory("user_profile", preset));
    EXPECT_EQ(preset.id, "user_profile");
    EXPECT_FALSE(appbox::FindPresetDirectory("does_not_exist", preset));
}

TEST(PackModel, ImportFolderAcceptsValidFolder)
{
    TempDir temp;

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;

    const auto imports = model.ImportsOf("program_files");
    ASSERT_EQ(imports.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(imports[0].import_name, temp.Get().filename().wstring());
    EXPECT_EQ(imports[0].source_path, temp.Get().wstring());
    EXPECT_EQ(model.ImportsOf("user_profile").size(), static_cast<std::size_t>(0));
}

TEST(PackModel, ImportFolderRejectsUnknownPreset)
{
    TempDir temp;

    appbox::PackModel model;
    std::string error;
    EXPECT_FALSE(model.ImportFolder("does_not_exist", temp.Get().wstring(), error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, ImportFolderRejectsMissingFolder)
{
    appbox::PackModel model;
    std::string error;
    const auto missing = std::filesystem::temp_directory_path() / L"appbox-no-such-folder";
    EXPECT_FALSE(model.ImportFolder("program_files", missing.wstring(), error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, ImportFolderRejectsDuplicateName)
{
    TempDir parent1;
    TempDir parent2;
    const auto folder1 = MakeFolder(parent1.Get(), L"MyApp");
    const auto folder2 = MakeFolder(parent2.Get(), L"MyApp");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", folder1.wstring(), error)) << error;
    EXPECT_FALSE(model.ImportFolder("program_files", folder2.wstring(), error));
    EXPECT_NE(error.find("MyApp"), std::string::npos);
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
}

TEST(PackModel, ImportFolderAllowsSameNameInDifferentPresets)
{
    TempDir parent1;
    TempDir parent2;
    const auto folder1 = MakeFolder(parent1.Get(), L"MyApp");
    const auto folder2 = MakeFolder(parent2.Get(), L"MyApp");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", folder1.wstring(), error)) << error;
    EXPECT_TRUE(model.ImportFolder("user_profile", folder2.wstring(), error)) << error;
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.ImportsOf("user_profile").size(), static_cast<std::size_t>(1));
}

TEST(PackModel, SetMainProgramAcceptsExecutable)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", temp.Get().filename().wstring(), L"app.exe",
                                     error))
        << error;

    EXPECT_TRUE(model.HasMainProgram());
    EXPECT_EQ(model.MainProgramChoice().preset_id, "program_files");
    EXPECT_EQ(model.MainProgramChoice().import_name, temp.Get().filename().wstring());
    EXPECT_EQ(model.MainProgramChoice().relative_path, L"app.exe");
}

TEST(PackModel, SetMainProgramNormalizesSeparators)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_TRUE(model.SetMainProgram("program_files", temp.Get().filename().wstring(),
                                     L"/app.exe", error))
        << error;
    EXPECT_EQ(model.MainProgramChoice().relative_path, L"app.exe");
}

TEST(PackModel, SetMainProgramRejectsNonExecutable)
{
    TempDir temp;
    MakeFile(temp.Get(), L"readme.txt", "TXT");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_FALSE(model.SetMainProgram("program_files", temp.Get().filename().wstring(),
                                      L"readme.txt", error));
    EXPECT_NE(error.find(".exe"), std::string::npos);
}

TEST(PackModel, SetMainProgramRejectsMissingFile)
{
    TempDir temp;

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_FALSE(model.SetMainProgram("program_files", temp.Get().filename().wstring(),
                                      L"missing.exe", error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, SetMainProgramRejectsEscapingPath)
{
    TempDir temp;

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    EXPECT_FALSE(model.SetMainProgram("program_files", temp.Get().filename().wstring(),
                                      L"..\\outside.exe", error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, SetMainProgramRejectsUnknownImport)
{
    appbox::PackModel model;
    std::string error;
    EXPECT_FALSE(model.SetMainProgram("program_files", L"Missing", L"app.exe", error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(model.HasMainProgram());
}

TEST(PackModel, RemoveImportClearsReferencedMainProgram)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", temp.Get().filename().wstring(), L"app.exe",
                                     error))
        << error;
    EXPECT_TRUE(model.HasMainProgram());

    model.RemoveImport("program_files", temp.Get().filename().wstring());
    EXPECT_FALSE(model.HasMainProgram());
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(0));
}

TEST(PackModel, RemoveImportKeepsUnrelatedMainProgram)
{
    TempDir temp1;
    TempDir temp2;
    MakeFile(temp1.Get(), L"one.exe", "ONE");
    MakeFile(temp2.Get(), L"two.exe", "TWO");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp1.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("user_profile", temp2.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", temp1.Get().filename().wstring(), L"one.exe",
                                     error))
        << error;

    model.RemoveImport("user_profile", temp2.Get().filename().wstring());
    EXPECT_TRUE(model.HasMainProgram());
    EXPECT_EQ(model.ImportsOf("user_profile").size(), static_cast<std::size_t>(0));
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
}

TEST(PackModel, MainProgramPathResolvesHostFile)
{
    TempDir temp;
    const auto exe = MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", temp.Get().filename().wstring(), L"app.exe",
                                     error))
        << error;

    std::wstring path;
    EXPECT_TRUE(model.MainProgramPath(path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(),
              exe.lexically_normal());
}

TEST(PackModel, ImportFilesAcceptsFileInsideImportedFolder)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
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

TEST(PackModel, ImportFilesNormalizesNestedTargetDirectory)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFolder(app, L"data");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"/MyApp/data/", { tool.wstring() }, error))
        << error;

    const auto files = model.FilesOf("program_files", L"MyApp\\data");
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].target_dir, L"MyApp\\data");
}

TEST(PackModel, ImportFilesKeepsSelectionOrder)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto first = MakeFile(temp.Get(), L"first.dll", "1");
    const auto second = MakeFile(temp.Get(), L"second.dll", "2");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp",
                                  { second.wstring(), first.wstring() }, error))
        << error;

    const auto files = model.AllImportedFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(files[0].file_name, L"second.dll");
    EXPECT_EQ(files[1].file_name, L"first.dll");
}

TEST(PackModel, ImportFilesRejectsUnknownPreset)
{
    TempDir temp;
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    EXPECT_FALSE(model.ImportFiles("does_not_exist", L"MyApp", { tool.wstring() }, error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, ImportFilesRejectsTargetOutsideAnImportedFolder)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"Other", { tool.wstring() }, error));
    EXPECT_NE(error.find("not an imported folder"), std::string::npos);
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(PackModel, ImportFilesRejectsEscapingTargetDirectory)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp\\..\\..", { tool.wstring() }, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(PackModel, ImportFilesRejectsMissingSource)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto missing = temp.Get() / L"missing.dll";

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { missing.wstring() }, error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, ImportFilesRejectsEmptySelection)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", {}, error));
    EXPECT_FALSE(error.empty());
}

TEST(PackModel, ImportFilesRejectsDuplicateImport)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(1));
}

TEST(PackModel, ImportFilesRejectsNameAlreadyInTheImportedFolder)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"tool.exe", "HOST");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error));
    EXPECT_NE(error.find("already exists in the imported folder"), std::string::npos);
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(PackModel, ImportFilesIsAtomic)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto good = MakeFile(temp.Get(), L"good.dll", "GOOD");
    const auto missing = temp.Get() / L"missing.dll";

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp",
                                   { good.wstring(), missing.wstring() }, error));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(PackModel, RemoveImportedFileRemovesTheEntry)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error)) << error;

    EXPECT_TRUE(model.RemoveImportedFile("program_files", L"MyApp", L"tool.exe"));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
    EXPECT_FALSE(model.RemoveImportedFile("program_files", L"MyApp", L"tool.exe"));
}

TEST(PackModel, RemoveImportCascadesImportedFiles)
{
    TempDir temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto other = MakeFolder(temp.Get(), L"OtherApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("program_files", other.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\data", { tool.wstring() }, error))
        << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"OtherApp", { tool.wstring() }, error)) << error;
    ASSERT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(2));

    model.RemoveImport("program_files", L"MyApp");

    const auto remaining = model.AllImportedFiles();
    ASSERT_EQ(remaining.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(remaining[0].target_dir, L"OtherApp");
}
