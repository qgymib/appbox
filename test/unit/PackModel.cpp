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
    /* The table of the preset directories, in the order it is resolved in. */
    struct ExpectedPreset
    {
        const char*    id;
        const char*    parent_id;
        const wchar_t* display_name;
        const wchar_t* layer_key;
    };
    const ExpectedPreset expected[] = {
        { "program_files",              "",                  L"Program Files",              L"#ProgramFiles#"          },
        { "program_files_common",       "program_files",     L"Common",                     L"#ProgramFilesCommon#"    },
        { "program_files_x86",          "",                  L"Program Files (x86)",        L"#ProgramFilesX86#"       },
        { "program_files_common_x86",   "program_files_x86", L"Common",                     L"#ProgramFilesCommonX86#" },
        { "user_profile",               "",                  L"Current User Directory",     L"#USERPROFILE#"           },
        { "documents",                  "user_profile",      L"Documents",                  L"#Documents#"             },
        { "desktop",                    "user_profile",      L"Desktop",                    L"#Desktop#"               },
        { "application_data",           "user_profile",      L"Application Data",           L"#AppData#"               },
        { "local_application_data",     "user_profile",      L"Local Application Data",     L"#LocalAppData#"          },
        { "local_application_data_low", "user_profile",      L"Local Application Data Low", L"#LocalAppDataLow#"       },
        { "downloads",                  "user_profile",      L"Downloads",                  L"#Downloads#"             },
        { "favorites",                  "user_profile",      L"Favorites",                  L"#Favorites#"             },
        { "music",                      "user_profile",      L"Music",                      L"#Music#"                 },
        { "pictures",                   "user_profile",      L"Pictures",                   L"#Pictures#"              },
        { "start_menu",                 "user_profile",      L"Start Menu",                 L"#StartMenu#"             },
        { "programs",                   "start_menu",        L"Programs",                   L"#Programs#"              },
        { "startup",                    "programs",          L"Startup",                    L"#Startup#"               },
        { "program_data",               "",                  L"Program Data",               L"#ProgramData#"           },
        { "windows",                    "",                  L"Windows",                    L"#Windows#"               },
        { "system32",                   "windows",           L"System32",                   L"#System32#"              },
        { "fonts",                      "windows",           L"Fonts",                      L"#Fonts#"                 },
    };

    const auto& presets = appbox::PresetDirectories();
    ASSERT_EQ(presets.size(), std::size(expected));

    for (std::size_t index = 0; index < std::size(expected); ++index)
    {
        EXPECT_EQ(presets[index].id, expected[index].id);
        EXPECT_EQ(presets[index].parent_id, expected[index].parent_id);
        EXPECT_EQ(presets[index].display_name, expected[index].display_name);
        EXPECT_EQ(presets[index].layer_key, expected[index].layer_key);

        /* Every preset resolves to a real folder of this machine. */
        EXPECT_TRUE(std::filesystem::path(presets[index].real_path).is_absolute());
    }

    /*
     * The nesting mirrors the host filesystem: the system directory is the
     * subdirectory of the Windows directory. The comparison resolves both
     * paths, so it does not depend on the spelling of a component.
     */
    appbox::PresetDirectory windows;
    appbox::PresetDirectory system32;
    ASSERT_TRUE(appbox::FindPresetDirectory("windows", windows));
    ASSERT_TRUE(appbox::FindPresetDirectory("system32", system32));

    std::error_code ec;
    EXPECT_TRUE(
        std::filesystem::equivalent(windows.real_path, std::filesystem::path(system32.real_path).parent_path(), ec))
        << ec.message();

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

TEST(Unit_PresetDirectory, ChildPresetsNestThePresetTree)
{
    /* The siblings of every group are ordered by their label. */
    const auto top = appbox::ChildPresets("");
    ASSERT_EQ(top.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(top[0].id, "user_profile");
    EXPECT_EQ(top[0].display_name, L"Current User Directory");
    EXPECT_EQ(top[1].id, "program_data");
    EXPECT_EQ(top[1].display_name, L"Program Data");
    EXPECT_EQ(top[2].id, "program_files");
    EXPECT_EQ(top[2].display_name, L"Program Files");
    EXPECT_EQ(top[3].id, "program_files_x86");
    EXPECT_EQ(top[3].display_name, L"Program Files (x86)");
    EXPECT_EQ(top[4].id, "windows");
    EXPECT_EQ(top[4].display_name, L"Windows");

    const auto profile = appbox::ChildPresets("user_profile");
    ASSERT_EQ(profile.size(), static_cast<std::size_t>(10));
    EXPECT_EQ(profile[0].id, "application_data");
    EXPECT_EQ(profile[0].layer_key, L"#AppData#");
    EXPECT_EQ(profile[1].id, "desktop");
    EXPECT_EQ(profile[1].layer_key, L"#Desktop#");
    EXPECT_EQ(profile[2].id, "documents");
    EXPECT_EQ(profile[2].layer_key, L"#Documents#");
    EXPECT_EQ(profile[3].id, "downloads");
    EXPECT_EQ(profile[3].layer_key, L"#Downloads#");
    EXPECT_EQ(profile[4].id, "favorites");
    EXPECT_EQ(profile[4].layer_key, L"#Favorites#");
    EXPECT_EQ(profile[5].id, "local_application_data");
    EXPECT_EQ(profile[5].layer_key, L"#LocalAppData#");
    EXPECT_EQ(profile[6].id, "local_application_data_low");
    EXPECT_EQ(profile[6].layer_key, L"#LocalAppDataLow#");
    EXPECT_EQ(profile[7].id, "music");
    EXPECT_EQ(profile[7].layer_key, L"#Music#");
    EXPECT_EQ(profile[8].id, "pictures");
    EXPECT_EQ(profile[8].layer_key, L"#Pictures#");
    EXPECT_EQ(profile[9].id, "start_menu");
    EXPECT_EQ(profile[9].layer_key, L"#StartMenu#");

    const auto start_menu = appbox::ChildPresets("start_menu");
    ASSERT_EQ(start_menu.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(start_menu[0].id, "programs");
    EXPECT_EQ(start_menu[0].layer_key, L"#Programs#");

    const auto programs = appbox::ChildPresets("programs");
    ASSERT_EQ(programs.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(programs[0].id, "startup");
    EXPECT_EQ(programs[0].layer_key, L"#Startup#");

    const auto program_files = appbox::ChildPresets("program_files");
    ASSERT_EQ(program_files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(program_files[0].id, "program_files_common");
    EXPECT_EQ(program_files[0].layer_key, L"#ProgramFilesCommon#");

    const auto program_files_x86 = appbox::ChildPresets("program_files_x86");
    ASSERT_EQ(program_files_x86.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(program_files_x86[0].id, "program_files_common_x86");
    EXPECT_EQ(program_files_x86[0].layer_key, L"#ProgramFilesCommonX86#");

    const auto system = appbox::ChildPresets("windows");
    ASSERT_EQ(system.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(system[0].id, "fonts");
    EXPECT_EQ(system[0].layer_key, L"#Fonts#");
    EXPECT_EQ(system[1].id, "system32");
    EXPECT_EQ(system[1].layer_key, L"#System32#");

    /* A preset without nested presets and an unknown preset hold nothing. */
    const char* const empty[] = { "application_data",
                                  "desktop",
                                  "documents",
                                  "downloads",
                                  "favorites",
                                  "fonts",
                                  "local_application_data",
                                  "local_application_data_low",
                                  "music",
                                  "pictures",
                                  "program_data",
                                  "program_files_common",
                                  "program_files_common_x86",
                                  "startup",
                                  "system32",
                                  "does_not_exist" };
    for (const char* id : empty)
    {
        EXPECT_TRUE(appbox::ChildPresets(id).empty()) << id;
    }

    /*
     * The tree of the presets holds every preset exactly once, which is what
     * the filesystem workspace walks to build its items.
     */
    std::size_t              reachable = 0;
    std::vector<std::string> pending = { "" };
    while (!pending.empty())
    {
        const std::string parent = pending.back();
        pending.pop_back();
        for (const auto& preset : appbox::ChildPresets(parent))
        {
            ++reachable;
            pending.push_back(preset.id);
        }
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
    EXPECT_TRUE(appbox::FindPresetDirectory("application_data", preset));
    EXPECT_EQ(preset.layer_key, L"#AppData#");
    EXPECT_EQ(preset.parent_id, "user_profile");
    EXPECT_TRUE(appbox::FindPresetDirectory("local_application_data", preset));
    EXPECT_EQ(preset.layer_key, L"#LocalAppData#");
    EXPECT_TRUE(appbox::FindPresetDirectory("local_application_data_low", preset));
    EXPECT_EQ(preset.layer_key, L"#LocalAppDataLow#");
    EXPECT_TRUE(appbox::FindPresetDirectory("downloads", preset));
    EXPECT_EQ(preset.layer_key, L"#Downloads#");
    EXPECT_TRUE(appbox::FindPresetDirectory("favorites", preset));
    EXPECT_EQ(preset.layer_key, L"#Favorites#");
    EXPECT_TRUE(appbox::FindPresetDirectory("music", preset));
    EXPECT_EQ(preset.layer_key, L"#Music#");
    EXPECT_EQ(preset.parent_id, "user_profile");
    EXPECT_TRUE(appbox::FindPresetDirectory("pictures", preset));
    EXPECT_EQ(preset.layer_key, L"#Pictures#");
    EXPECT_EQ(preset.parent_id, "user_profile");
    EXPECT_TRUE(appbox::FindPresetDirectory("start_menu", preset));
    EXPECT_EQ(preset.layer_key, L"#StartMenu#");
    EXPECT_TRUE(appbox::FindPresetDirectory("programs", preset));
    EXPECT_EQ(preset.layer_key, L"#Programs#");
    EXPECT_EQ(preset.parent_id, "start_menu");
    EXPECT_TRUE(appbox::FindPresetDirectory("startup", preset));
    EXPECT_EQ(preset.layer_key, L"#Startup#");
    EXPECT_EQ(preset.parent_id, "programs");
    EXPECT_TRUE(appbox::FindPresetDirectory("program_data", preset));
    EXPECT_EQ(preset.layer_key, L"#ProgramData#");
    EXPECT_EQ(preset.parent_id, "");
    EXPECT_TRUE(appbox::FindPresetDirectory("program_files_common", preset));
    EXPECT_EQ(preset.layer_key, L"#ProgramFilesCommon#");
    EXPECT_EQ(preset.parent_id, "program_files");
    EXPECT_TRUE(appbox::FindPresetDirectory("program_files_x86", preset));
    EXPECT_EQ(preset.layer_key, L"#ProgramFilesX86#");
    EXPECT_EQ(preset.parent_id, "");
    EXPECT_TRUE(appbox::FindPresetDirectory("program_files_common_x86", preset));
    EXPECT_EQ(preset.layer_key, L"#ProgramFilesCommonX86#");
    EXPECT_EQ(preset.parent_id, "program_files_x86");
    EXPECT_TRUE(appbox::FindPresetDirectory("windows", preset));
    EXPECT_EQ(preset.layer_key, L"#Windows#");
    EXPECT_TRUE(appbox::FindPresetDirectory("system32", preset));
    EXPECT_EQ(preset.layer_key, L"#System32#");
    EXPECT_TRUE(appbox::FindPresetDirectory("fonts", preset));
    EXPECT_EQ(preset.layer_key, L"#Fonts#");
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

/**
 * @brief The `Up Dir` command of the filesystem workspace is offered for every
 *        node of the tree but the container.
 *
 * The container is the root of the tree and the only node without a parent, so
 * a top level preset directory moves to the container like a nested one moves
 * to the preset it hangs below. The panel asks for this rule both for the
 * enabled state of the button and for the node the command selects, which keeps
 * the two from drifting apart.
 */
TEST(Unit_PresetDirectory, FilesystemNodeHasParentExcludesTheContainer)
{
    appbox::PresetDirectory preset;

    /* The container carries no preset, which marks it as the root of the tree. */
    EXPECT_FALSE(appbox::FilesystemNodeHasParent(""));

    /*
     * A top level preset directory hangs below the container, a nested one
     * below the preset it belongs to; both are offered by the tree.
     */
    const char* const nodes[] = { "program_files",
                                  "program_data",
                                  "user_profile",
                                  "windows",
                                  "documents",
                                  "music",
                                  "pictures",
                                  "programs",
                                  "startup",
                                  "system32",
                                  "fonts",
                                  "program_files_x86",
                                  "program_files_common_x86" };
    for (const char* id : nodes)
    {
        ASSERT_TRUE(appbox::FindPresetDirectory(id, preset)) << id;
        EXPECT_TRUE(appbox::FilesystemNodeHasParent(id)) << id;
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

TEST(Unit_PackModel, AddStartupFileAcceptsEveryStartableFile)
{
    TempDir    temp;
    const auto bin = MakeFolder(temp.Get(), L"bin");
    MakeFile(temp.Get(), L"app.exe", "EXE");
    MakeFile(temp.Get(), L"run.bat", "BAT");
    MakeFile(temp.Get(), L"build.cmd", "CMD");
    MakeFile(temp.Get(), L"deploy.ps1", "PS1");
    MakeFile(bin, L"tool.CMD", "CMD");
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"app.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"run.bat", false, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"build.cmd", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"deploy.ps1", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", import_name, L"bin\\tool.CMD", true, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(files[0].relative_path, L"app.exe");
    EXPECT_EQ(files[0].trigger, L"app");
    EXPECT_TRUE(files[0].auto_start);

    EXPECT_EQ(files[1].relative_path, L"run.bat");
    EXPECT_EQ(files[1].trigger, L"run");
    EXPECT_FALSE(files[1].auto_start);

    EXPECT_EQ(files[2].relative_path, L"build.cmd");
    EXPECT_EQ(files[2].trigger, L"build");
    EXPECT_EQ(files[3].relative_path, L"deploy.ps1");
    EXPECT_EQ(files[3].trigger, L"deploy");

    /* The extension is recognized ignoring the case. */
    EXPECT_EQ(files[4].relative_path, L"bin\\tool.CMD");
    EXPECT_EQ(files[4].trigger, L"tool");

    EXPECT_TRUE(model.IsStartupFile("program_files", import_name, L"deploy.ps1"));
    EXPECT_TRUE(model.IsStartupFile("program_files", import_name, L"BIN\\TOOL.cmd"));
}

TEST(Unit_PackModel, AddStartupFileRejectsFilesWhichCannotBeStarted)
{
    TempDir temp;
    MakeFile(temp.Get(), L"readme.txt", "TXT");
    MakeFile(temp.Get(), L"library.dll", "DLL");
    MakeFile(temp.Get(), L"backup.exe.bak", "BAK");
    MakeFile(temp.Get(), L"noextension", "TXT");
    const auto import_name = temp.Get().filename().wstring();

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;

    const wchar_t* const refused[] = { L"readme.txt", L"library.dll", L"backup.exe.bak", L"noextension" };
    for (const auto* name : refused)
    {
        error.clear();
        EXPECT_FALSE(model.AddStartupFile("program_files", import_name, name, true, error)) << name;

        /* The message names the extensions which can be chosen. */
        EXPECT_NE(error.find(".exe"), std::string::npos) << error;
        EXPECT_NE(error.find(".ps1"), std::string::npos) << error;
    }

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

TEST(Unit_PackModel, AddStartupFileRejectsAFileOutsideTheView)
{
    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.AddStartupFile("program_files", L"Missing", L"app.exe", true, error));
    EXPECT_NE(error.find("not a file of the view"), std::string::npos);
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

/**
 * @brief A file a capture placed below a folder of the view can be started.
 *
 * The capture fills the entries an installation created at the very paths the
 * host holds them at, so a file of a folder which was never imported has to be
 * startable like a file of an import.
 */
TEST(Unit_PackModel, AddStartupFileAcceptsAFileBelowAFolderOfTheView)
{
    TempDir    temp;
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"", L"AppBoxVirtualFolder", error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"AppBoxVirtualFolder", { tool.wstring() }, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"AppBoxVirtualFolder", L"tool.exe", true, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].import_name, L"AppBoxVirtualFolder");
    EXPECT_EQ(files[0].relative_path, L"tool.exe");
    EXPECT_EQ(files[0].trigger, L"tool");

    std::wstring path;
    EXPECT_TRUE(model.StartupFilePath(files[0], path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(), tool.lexically_normal());
}

/**
 * @brief A file the capture placed in the layer root can be started.
 *
 * A file of the layer root is named by its own name alone: the launcher reads
 * the path of the file inside the layer, which carries no root segment.
 */
TEST(Unit_PackModel, AddStartupFileAcceptsAFileInTheLayerRoot)
{
    TempDir    temp;
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"", { tool.wstring() }, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"", L"tool.exe", true, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(files[0].import_name.empty());
    EXPECT_EQ(files[0].relative_path, L"tool.exe");

    std::wstring path;
    EXPECT_TRUE(model.StartupFilePath(files[0], path));
    EXPECT_EQ(std::filesystem::path(path).lexically_normal(), tool.lexically_normal());
}

TEST(Unit_PackModel, SetStartupFilesReplacesTheList)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFile(app, L"one.exe", "ONE");
    MakeFile(app, L"two.exe", "TWO");
    MakeFile(app, L"deploy.cmd", "CMD");

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

    /* A script is a startup file as well. */
    appbox::StartupFile second;
    second.preset_id = "program_files";
    second.import_name = L"MyApp";
    second.relative_path = L"deploy.cmd";
    second.trigger = L"deploy";
    second.auto_start = true;

    ASSERT_TRUE(model.SetStartupFiles({ first, second }, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(files[0].import_name, L"MyApp");
    EXPECT_EQ(files[0].relative_path, L"two.exe");
    EXPECT_EQ(files[0].trigger, L"second");
    EXPECT_FALSE(files[0].auto_start);
    EXPECT_EQ(files[1].relative_path, L"deploy.cmd");
    EXPECT_EQ(files[1].trigger, L"deploy");
    EXPECT_TRUE(files[1].auto_start);
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

TEST(Unit_PackModel, ImportFilesAcceptsAFileInTheLayerRoot)
{
    TempDir    temp;
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"", { tool.wstring() }, error)) << error;

    const auto files = model.FilesOf("program_files", L"");
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(files[0].target_dir.empty());
    EXPECT_EQ(files[0].file_name, L"tool.exe");
    EXPECT_EQ(files[0].source_path, tool.wstring());
}

TEST(Unit_PackModel, ImportFilesAcceptsAFileBelowAFolderOfTheView)
{
    TempDir    temp;
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"", L"AppBoxVirtualFolder", error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"AppBoxVirtualFolder", { tool.wstring() }, error)) << error;

    const auto files = model.FilesOf("program_files", L"AppBoxVirtualFolder");
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].file_name, L"tool.exe");
}

TEST(Unit_PackModel, ImportFilesRejectsAFileTheImportedFolderCarries)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(app, L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    /* The import carries the whole host folder, so the file is in the archive already. */
    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { tool.wstring() }, error));
    EXPECT_NE(error.find("already exists in the imported folder"), std::string::npos);
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
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

TEST(Unit_PackModel, ImportFilesRejectsTargetOutsideTheView)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    /* The folder is neither an import nor a folder the user created. */
    EXPECT_FALSE(model.ImportFiles("program_files", L"Other", { tool.wstring() }, error));
    EXPECT_NE(error.find("not a folder of the view"), std::string::npos);
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
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin", L"old.dll", error)) << error;
    EXPECT_FALSE(model.IsEmpty());

    model.Clear();

    EXPECT_TRUE(model.IsEmpty());
    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(0));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
    EXPECT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(0));
    EXPECT_EQ(model.AllWhiteouts().size(), static_cast<std::size_t>(0));
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

TEST(Unit_PackModel, RestoreImportedFileRejectsTargetOutsideTheView)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;

    EXPECT_FALSE(model.RestoreImportedFile("program_files", L"Other", L"tool.exe", L"C:\\tool.exe", error));
    EXPECT_NE(error.find("not a folder of the view"), std::string::npos);
    EXPECT_FALSE(model.RestoreImportedFile("program_files", L"MyApp\\..", L"tool.exe", L"C:\\tool.exe", error));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(0));
}

TEST(Unit_PackModel, RestoreImportedFileAcceptsAFileInTheLayerRoot)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFile("program_files", L"", L"tool.exe", L"C:\\tool.exe", error)) << error;

    const auto files = model.AllImportedFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(files[0].target_dir.empty());
    EXPECT_EQ(files[0].file_name, L"tool.exe");
    EXPECT_EQ(files[0].source_path, L"C:\\tool.exe");
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

TEST(Unit_PackModel, RestoreStartupFileAcceptsAScript)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\MyApp", error)) << error;
    ASSERT_TRUE(model.RestoreStartupFile("program_files", L"MyApp", L"bin\\deploy.ps1", L"deploy", true, error))
        << error;
    ASSERT_TRUE(model.RestoreStartupFile("program_files", L"MyApp", L"run.bat", L"run", false, error)) << error;

    const auto& files = model.StartupFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(files[0].relative_path, L"bin\\deploy.ps1");
    EXPECT_EQ(files[0].trigger, L"deploy");
    EXPECT_TRUE(files[0].auto_start);
    EXPECT_EQ(files[1].relative_path, L"run.bat");
    EXPECT_FALSE(files[1].auto_start);
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

TEST(Unit_PackModel, CreateVirtualDirectoryBelowTheLayerRoot)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;

    const auto directories = model.AllVirtualDirectories();
    ASSERT_EQ(directories.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(directories[0].preset_id, "program_files");
    EXPECT_EQ(directories[0].target_dir, L"appbox-logs");

    /* A path of the view is matched ignoring the case, like every other lookup. */
    EXPECT_TRUE(model.HasVirtualDirectory("program_files", L"APPBOX-LOGS"));
    EXPECT_FALSE(model.HasVirtualDirectory("user_profile", L"appbox-logs"));
    EXPECT_FALSE(model.HasVirtualDirectory("program_files", std::wstring()));

    EXPECT_FALSE(model.IsEmpty());
    EXPECT_EQ(model.VirtualDirectoriesOf("program_files", std::wstring()).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.VirtualDirectoriesOf("program_files", L"appbox-logs").size(), static_cast<std::size_t>(0));

    /* A folder the user created is neither an import nor a file of the host. */
    EXPECT_TRUE(model.ImportsOf("program_files").empty());
    EXPECT_TRUE(model.AllImportedFiles().empty());
}

TEST(Unit_PackModel, CreateVirtualDirectoryRejectsUnknownPreset)
{
    appbox::PackModel model;
    std::string       error;
    EXPECT_FALSE(model.CreateVirtualDirectory("does_not_exist", std::wstring(), L"logs", error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_PackModel, CreateVirtualDirectoryRejectsUnusableName)
{
    appbox::PackModel model;
    std::string       error;

    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), std::wstring(), error));
    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), L".", error));
    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), L"..", error));
    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), L"logs\\cache", error));
    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), L"logs/cache", error));
    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_PackModel, CreateVirtualDirectoryRejectsDuplicateName)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;

    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), L"APPBOX-LOGS", error));
    EXPECT_NE(error.find("folder of the view"), std::string::npos);
    EXPECT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(1));

    /* The same name below another preset directory is a folder of its own. */
    EXPECT_TRUE(model.CreateVirtualDirectory("user_profile", std::wstring(), L"appbox-logs", error)) << error;
}

TEST(Unit_PackModel, CreateVirtualDirectoryRejectsAnImportedFolderName)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", std::wstring(), L"MyApp", error));
    EXPECT_NE(error.find("imported folder"), std::string::npos);
    EXPECT_TRUE(model.AllVirtualDirectories().empty());
}

TEST(Unit_PackModel, CreateVirtualDirectoryRejectsANameOfTheHostFolder)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFolder(app, L"logs");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    /*
     * The folder of the host would show up twice in the view of the directory,
     * because the layer is merged with the imported folder at run time.
     */
    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error));
    EXPECT_NE(error.find("host folder"), std::string::npos);
    EXPECT_TRUE(model.AllVirtualDirectories().empty());
}

TEST(Unit_PackModel, CreateVirtualDirectoryBelowAnImport)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFolder(app, L"bin");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    const auto import_name = app.filename().wstring();

    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name, L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name + L"\\bin", L"cache", error)) << error;

    const auto directories = model.AllVirtualDirectories();
    ASSERT_EQ(directories.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(directories[0].target_dir, L"MyApp\\logs");
    EXPECT_EQ(directories[1].target_dir, L"MyApp\\bin\\cache");

    /* A folder which is not part of the view is refused as a parent. */
    EXPECT_FALSE(model.CreateVirtualDirectory("program_files", import_name + L"\\missing", L"cache", error));
    EXPECT_NE(error.find("not a folder of the view"), std::string::npos);
    EXPECT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(2));
}

TEST(Unit_PackModel, CreateVirtualDirectoryBelowACreatedFolder)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"appbox-logs", L"cache", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"appbox-logs\\cache", L"deep", error)) << error;

    EXPECT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(3));
    EXPECT_TRUE(model.HasVirtualDirectory("program_files", L"APPBOX-LOGS\\CACHE\\DEEP"));

    /* Only the direct children of a directory are reported. */
    const auto children = model.VirtualDirectoriesOf("program_files", L"appbox-logs");
    ASSERT_EQ(children.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(children[0].target_dir, L"appbox-logs\\cache");
    EXPECT_EQ(model.VirtualDirectoriesOf("program_files", L"appbox-logs\\cache").size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, FreeVirtualDirectoryNameDerivesUniqueNames)
{
    appbox::PackModel model;
    std::string       error;

    EXPECT_EQ(model.FreeVirtualDirectoryName("program_files", std::wstring()), L"New Folder");

    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"New Folder", error)) << error;
    EXPECT_EQ(model.FreeVirtualDirectoryName("program_files", std::wstring()), L"New Folder 2");

    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"new folder 2", error)) << error;
    EXPECT_EQ(model.FreeVirtualDirectoryName("program_files", std::wstring()), L"New Folder 3");

    /* The name is asked for inside one directory, so another one is free. */
    EXPECT_EQ(model.FreeVirtualDirectoryName("program_files", L"New Folder"), L"New Folder");

    /* An imported folder of the directory takes the name as well. */
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"New Folder 3");
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    EXPECT_EQ(model.FreeVirtualDirectoryName("program_files", std::wstring()), L"New Folder 4");

    /* The name of the import itself is free below the import. */
    EXPECT_EQ(model.FreeVirtualDirectoryName("program_files", L"New Folder 3"), L"New Folder");
}

TEST(Unit_PackModel, RenameVirtualDirectoryRenamesTheSubtree)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"appbox-logs", L"cache", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"appbox-logs\\cache", L"deep", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-other", error)) << error;

    ASSERT_TRUE(model.RenameVirtualDirectory("program_files", L"appbox-logs", L"appbox-data", error)) << error;

    const auto directories = model.AllVirtualDirectories();
    ASSERT_EQ(directories.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(directories[0].target_dir, L"appbox-data");
    EXPECT_EQ(directories[1].target_dir, L"appbox-data\\cache");
    EXPECT_EQ(directories[2].target_dir, L"appbox-data\\cache\\deep");
    EXPECT_EQ(directories[3].target_dir, L"appbox-other");

    EXPECT_TRUE(model.HasVirtualDirectory("program_files", L"appbox-data\\cache\\deep"));
    EXPECT_FALSE(model.HasVirtualDirectory("program_files", L"appbox-logs"));
}

TEST(Unit_PackModel, RenameVirtualDirectoryAcceptsTheSameName)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;

    ASSERT_TRUE(model.RenameVirtualDirectory("program_files", L"appbox-logs", L"APPBOX-LOGS", error)) << error;

    ASSERT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.AllVirtualDirectories()[0].target_dir, L"appbox-logs");
}

TEST(Unit_PackModel, RenameVirtualDirectoryRejectsATakenName)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-data", error)) << error;

    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", L"appbox-logs", L"appbox-data", error));
    EXPECT_NE(error.find("folder of the view"), std::string::npos);

    /* A refusal leaves the model as it was. */
    EXPECT_TRUE(model.HasVirtualDirectory("program_files", L"appbox-logs"));
    EXPECT_TRUE(model.HasVirtualDirectory("program_files", L"appbox-data"));
}

TEST(Unit_PackModel, RenameVirtualDirectoryRejectsAFolderTheUserDidNotCreate)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;

    /* An imported folder and the layer root are not folders of the user. */
    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", L"MyApp", L"Other", error));
    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", std::wstring(), L"Other", error));
    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", L"Missing", L"Other", error));
    EXPECT_NE(error.find("not a folder of the view"), std::string::npos);
}

TEST(Unit_PackModel, RenameVirtualDirectoryRejectsAnUnusableName)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;

    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", L"appbox-logs", L"..", error));
    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", L"appbox-logs", L"a\\b", error));
    EXPECT_FALSE(model.RenameVirtualDirectory("program_files", L"appbox-logs", std::wstring(), error));
    EXPECT_TRUE(model.HasVirtualDirectory("program_files", L"appbox-logs"));
}

TEST(Unit_PackModel, RemoveVirtualDirectoryRemovesTheFilesBelowIt)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto tool = MakeFile(temp.Get(), L"tool.exe", "TOOL");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp\\logs", L"cache", error)) << error;

    /* An imported file lives in the folder, so the folder is a lower layer. */
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\logs", { tool.wstring() }, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"logs\\tool.exe", true, error)) << error;

    EXPECT_TRUE(model.RemoveVirtualDirectory("program_files", L"MyApp\\logs"));

    EXPECT_TRUE(model.AllVirtualDirectories().empty());
    EXPECT_TRUE(model.AllImportedFiles().empty());
    EXPECT_FALSE(model.HasStartupFiles());

    /* A folder which the user did not create is not removed. */
    EXPECT_FALSE(model.RemoveVirtualDirectory("program_files", L"MyApp"));
}

TEST(Unit_PackModel, RemoveImportCascadesVirtualDirectories)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto other = MakeFolder(temp.Get(), L"OtherApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("program_files", other.wstring(), error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"OtherApp", L"logs", error)) << error;
    ASSERT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(2));

    model.RemoveImport("program_files", L"MyApp");

    const auto directories = model.AllVirtualDirectories();
    ASSERT_EQ(directories.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(directories[0].target_dir, L"OtherApp\\logs");
}

TEST(Unit_PackModel, ImportFilesRejectsANameOfACreatedFolder)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto logs = MakeFile(temp.Get(), L"logs", "LOGS");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;

    /*
     * A file and a folder of the same path would be two entries of one name
     * inside the archive, which the zip writer refuses.
     */
    EXPECT_FALSE(model.ImportFiles("program_files", L"MyApp", { logs.wstring() }, error));
    EXPECT_NE(error.find("folder of the view"), std::string::npos);
    EXPECT_TRUE(model.AllImportedFiles().empty());
}

TEST(Unit_PackModel, RestoreVirtualDirectoryAcceptsAMissingHostFolder)
{
    appbox::PackModel model;
    std::string       error;

    /* The import is restored from the document, its host folder may be gone. */
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\Missing\\MyApp", error)) << error;
    ASSERT_TRUE(model.RestoreVirtualDirectory("program_files", L"myapp\\logs", error)) << error;
    ASSERT_TRUE(model.RestoreVirtualDirectory("program_files", L"MyApp\\logs\\cache", error)) << error;

    const auto directories = model.AllVirtualDirectories();
    ASSERT_EQ(directories.size(), static_cast<std::size_t>(2));

    /* The stored path uses the canonical spelling of the imported folder. */
    EXPECT_EQ(directories[0].target_dir, L"MyApp\\logs");
    EXPECT_EQ(directories[1].target_dir, L"MyApp\\logs\\cache");
    EXPECT_FALSE(model.IsEmpty());
}

TEST(Unit_PackModel, RestoreVirtualDirectoryRejectsInvalidPaths)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\Missing\\MyApp", error)) << error;

    EXPECT_FALSE(model.RestoreVirtualDirectory("does_not_exist", L"MyApp\\logs", error));
    EXPECT_FALSE(model.RestoreVirtualDirectory("program_files", std::wstring(), error));
    EXPECT_FALSE(model.RestoreVirtualDirectory("program_files", L"..\\logs", error));

    /* The parent has to be a folder of the view. */
    EXPECT_FALSE(model.RestoreVirtualDirectory("program_files", L"Missing\\logs", error));
    EXPECT_NE(error.find("not a folder of the view"), std::string::npos);

    ASSERT_TRUE(model.RestoreVirtualDirectory("program_files", L"MyApp\\logs", error)) << error;
    EXPECT_FALSE(model.RestoreVirtualDirectory("program_files", L"myapp\\LOGS", error));
    EXPECT_NE(error.find("listed twice"), std::string::npos);
    EXPECT_EQ(model.AllVirtualDirectories().size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, AddWhiteoutHidesAnEntryOfTheLayerRootAndOfAFolder)
{
    appbox::PackModel model;
    std::string       error;

    EXPECT_TRUE(model.IsEmpty());

    ASSERT_TRUE(model.AddWhiteout("program_files", std::wstring(), L"appbox-legacy.exe", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin", L"old.dll", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("user_profile", std::wstring(), L"appbox-legacy.exe", error)) << error;

    EXPECT_FALSE(model.IsEmpty());

    const auto all = model.AllWhiteouts();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(all[0].preset_id, "program_files");
    EXPECT_EQ(all[0].target_dir, std::wstring());
    EXPECT_EQ(all[0].name, L"appbox-legacy.exe");
    EXPECT_EQ(all[1].target_dir, L"MyApp\\bin");
    EXPECT_EQ(all[1].name, L"old.dll");
    EXPECT_EQ(all[2].preset_id, "user_profile");

    /* Only the direct children of a directory are reported. */
    const auto root = model.WhiteoutsOf("program_files", std::wstring());
    ASSERT_EQ(root.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(root[0].name, L"appbox-legacy.exe");

    const auto bin = model.WhiteoutsOf("program_files", L"myapp\\BIN");
    ASSERT_EQ(bin.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(bin[0].name, L"old.dll");

    EXPECT_TRUE(model.WhiteoutsOf("program_files", L"MyApp").empty());

    /* A path of the view is matched ignoring the case, like every other lookup. */
    EXPECT_TRUE(model.HasWhiteout("program_files", L"APPBOX-LEGACY.EXE"));
    EXPECT_TRUE(model.HasWhiteout("program_files", L"MyApp\\bin\\OLD.DLL"));
    EXPECT_FALSE(model.HasWhiteout("program_files", L"Other.exe"));
    EXPECT_FALSE(model.HasWhiteout("user_profile", L"MyApp\\bin\\old.dll"));
}

TEST(Unit_PackModel, AddWhiteoutNormalizesTheTargetDirectory)
{
    appbox::PackModel model;
    std::string       error;

    ASSERT_TRUE(model.AddWhiteout("program_files", L"/MyApp//bin/", L"old.dll", error)) << error;

    const auto all = model.AllWhiteouts();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(all[0].target_dir, L"MyApp\\bin");
}

TEST(Unit_PackModel, AddWhiteoutRejectsInvalidRecords)
{
    appbox::PackModel model;
    std::string       error;

    EXPECT_FALSE(model.AddWhiteout("does_not_exist", std::wstring(), L"old.dll", error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(model.AddWhiteout("program_files", L"..\\MyApp", L"old.dll", error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(model.AddWhiteout("program_files", std::wstring(), std::wstring(), error));
    EXPECT_FALSE(model.AddWhiteout("program_files", std::wstring(), L"..", error));
    EXPECT_FALSE(model.AddWhiteout("program_files", std::wstring(), L"bin\\old.dll", error));

    /*
     * The names the view reserves for its markers name no entry which could be
     * hidden.
     */
    EXPECT_FALSE(model.AddWhiteout("program_files", std::wstring(), L".$APPBOX_OPAQUE$", error));
    EXPECT_FALSE(model.AddWhiteout("program_files", std::wstring(), L"old.dll.$APPBOX_DELETE$", error));

    EXPECT_TRUE(model.IsEmpty());
}

TEST(Unit_PackModel, AddWhiteoutRejectsAnEntryWhichIsHiddenAlready)
{
    appbox::PackModel model;
    std::string       error;

    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp", L"old.dll", error)) << error;

    EXPECT_FALSE(model.AddWhiteout("program_files", L"MyApp", L"OLD.DLL", error));
    EXPECT_NE(error.find("hidden already"), std::string::npos);
    EXPECT_EQ(model.AllWhiteouts().size(), static_cast<std::size_t>(1));
}

TEST(Unit_PackModel, RemoveWhiteoutDropsTheEntry)
{
    appbox::PackModel model;
    std::string       error;

    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp", L"old.dll", error)) << error;

    EXPECT_TRUE(model.RemoveWhiteout("program_files", L"myapp", L"OLD.DLL"));
    EXPECT_TRUE(model.AllWhiteouts().empty());
    EXPECT_TRUE(model.IsEmpty());

    EXPECT_FALSE(model.RemoveWhiteout("program_files", L"MyApp", L"old.dll"));
}

TEST(Unit_PackModel, RemoveWhiteoutsUnderDropsTheSubtree)
{
    appbox::PackModel model;
    std::string       error;

    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp", L"old.dll", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin", L"deep.dll", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin\\x64", L"deepest.dll", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"Other", L"other.dll", error)) << error;

    EXPECT_TRUE(model.RemoveWhiteoutsUnder("program_files", L"myapp\\bin"));

    const auto all = model.AllWhiteouts();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(all[0].name, L"old.dll");
    EXPECT_EQ(all[1].name, L"other.dll");

    /* An empty directory names the layer root, which holds every entry. */
    EXPECT_TRUE(model.RemoveWhiteoutsUnder("program_files", std::wstring()));
    EXPECT_TRUE(model.AllWhiteouts().empty());
}

TEST(Unit_PackModel, RemoveImportCascadesWhiteouts)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    const auto other = MakeFolder(temp.Get(), L"OtherApp");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("program_files", other.wstring(), error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin", L"old.dll", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"OtherApp\\bin", L"old.dll", error)) << error;

    /*
     * The entry which hides the imported folder itself hangs below the layer
     * root and not below the import, so it stays.
     */
    ASSERT_TRUE(model.AddWhiteout("program_files", std::wstring(), L"MyApp", error)) << error;

    model.RemoveImport("program_files", L"MyApp");

    const auto all = model.AllWhiteouts();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(all[0].target_dir, L"OtherApp\\bin");
    EXPECT_EQ(all[0].name, L"old.dll");
    EXPECT_EQ(all[1].preset_id, "program_files");
    EXPECT_EQ(all[1].target_dir, std::wstring());
    EXPECT_EQ(all[1].name, L"MyApp");
}

TEST(Unit_PackModel, RemoveVirtualDirectoryCascadesWhiteouts)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", std::wstring(), L"appbox-logs", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"appbox-logs", L"old.log", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"appbox-logs\\deep", L"old.log", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", std::wstring(), L"appbox-other.log", error)) << error;

    EXPECT_TRUE(model.RemoveVirtualDirectory("program_files", L"appbox-logs"));

    const auto all = model.AllWhiteouts();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(all[0].name, L"appbox-other.log");
}

TEST(Unit_PackModel, IsDirectoryOfViewReportsTheFoldersOfTheView)
{
    TempDir    temp;
    const auto app = MakeFolder(temp.Get(), L"MyApp");
    MakeFolder(app, L"bin");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", app.wstring(), error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;

    /* The layer root, the import root, a folder below the import and a created folder. */
    EXPECT_TRUE(model.IsDirectoryOfView("program_files", std::wstring()));
    EXPECT_TRUE(model.IsDirectoryOfView("program_files", L"myapp"));
    EXPECT_TRUE(model.IsDirectoryOfView("program_files", L"MyApp\\bin"));
    EXPECT_TRUE(model.IsDirectoryOfView("program_files", L"MyApp\\logs"));
    EXPECT_TRUE(model.IsDirectoryOfView("program_files", L"MyApp\\logs\\deep"));

    EXPECT_FALSE(model.IsDirectoryOfView("program_files", L"Missing"));
    EXPECT_FALSE(model.IsDirectoryOfView("program_files", L"Missing\\deep"));
    EXPECT_FALSE(model.IsDirectoryOfView("program_files", L"..\\MyApp"));
    EXPECT_FALSE(model.IsDirectoryOfView("does_not_exist", std::wstring()));
}

TEST(Unit_PackModel, RestoreWhiteoutAcceptsAnEntryWhichIsNotOnTheHost)
{
    appbox::PackModel model;
    std::string       error;

    /*
     * The record of a document names an entry of the host which does not have
     * to exist on the machine which reads the document.
     */
    ASSERT_TRUE(model.RestoreWhiteout("program_files", L"Missing\\bin", L"old.dll", error)) << error;

    const auto all = model.AllWhiteouts();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(all[0].target_dir, L"Missing\\bin");
    EXPECT_FALSE(model.IsEmpty());

    EXPECT_FALSE(model.RestoreWhiteout("program_files", L"Missing\\bin", L"old.dll", error));
    EXPECT_NE(error.find("hidden already"), std::string::npos);
}
