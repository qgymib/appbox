#include <gtest/gtest.h>
#include "src/core/PackModel.hpp"
#include "src/core/StartupTree.hpp"
#include <chrono>
#include <filesystem>
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
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-startuptree-" + UniqueFragment());
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

/**
 * @brief A pack model with one imported folder below the Program Files preset.
 */
class ImportedApp
{
public:
    ImportedApp()
    {
        import_name_ = folder_.Get().filename().wstring();

        std::string error;
        imported_ = model_.ImportFolder("program_files", folder_.Get().wstring(), error);
    }

    /**
     * @brief Whether the import was accepted by the model.
     * @return true when the folder was imported.
     */
    bool Imported() const
    {
        return imported_;
    }

    /**
     * @brief Get the imported folder.
     * @return The host folder which was imported.
     */
    const std::filesystem::path& Folder() const
    {
        return folder_.Get();
    }

    /**
     * @brief Get the model holding the import.
     * @return The pack model.
     */
    const appbox::PackModel& Model() const
    {
        return model_;
    }

    /**
     * @brief Get the name of the imported folder.
     * @return The import name.
     */
    const std::wstring& ImportName() const
    {
        return import_name_;
    }

private:
    TempDir           folder_;
    appbox::PackModel model_;
    std::wstring      import_name_;
    bool              imported_ = false;
};

/**
 * @brief Get the root row of one preset directory.
 * @param[in] tree Tree to search.
 * @param[in] preset_id Identifier of the preset directory.
 * @return The preset row, null when the tree has none.
 */
appbox::StartupNode* PresetRow(appbox::StartupTree& tree, const std::string& preset_id)
{
    for (const auto& root : tree.Roots())
    {
        if (root->preset_id == preset_id)
        {
            return root.get();
        }
    }
    return nullptr;
}

/**
 * @brief Get the import row of the Program Files preset.
 * @param[in] tree Tree to search.
 * @return The import row, null when the tree has none.
 */
appbox::StartupNode* ImportRow(appbox::StartupTree& tree)
{
    appbox::StartupNode* const preset = PresetRow(tree, "program_files");
    return preset != nullptr && !preset->children.empty() ? preset->children.front().get() : nullptr;
}

/**
 * @brief Get the labels of the children of one row.
 * @param[in] node Row to inspect.
 * @return The labels in tree order.
 */
std::vector<std::wstring> ChildLabels(const appbox::StartupNode& node)
{
    std::vector<std::wstring> labels;
    for (const auto& child : node.children)
    {
        labels.push_back(child->label);
    }
    return labels;
}

} // namespace

TEST(Unit_StartupTree, BuildsPresetAndImportRows)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    appbox::StartupTree tree(app.Model());

    /* One root per preset directory, the import hangs below its preset. */
    ASSERT_EQ(tree.Roots().size(), appbox::PresetDirectories().size());

    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    EXPECT_EQ(import->kind, appbox::StartupNodeKind::Import);
    EXPECT_EQ(import->label, app.ImportName());
    EXPECT_EQ(import->preset_id, "program_files");
    EXPECT_EQ(import->import_name, app.ImportName());
    EXPECT_EQ(import->host_path, app.Folder().wstring());
    EXPECT_TRUE(import->relative_path.empty());
    EXPECT_FALSE(import->populated);
}

TEST(Unit_StartupTree, PresetRowsKeepTheirImports)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const preset = PresetRow(tree, "program_files");
    ASSERT_NE(preset, nullptr);

    const auto imports = ChildLabels(*preset);
    ASSERT_EQ(imports.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(imports.front(), app.ImportName());

    /*
     * The children of a preset are its imports: the host content of the preset
     * folder must never replace them.
     */
    tree.EnsureChildren(*preset);

    EXPECT_EQ(ChildLabels(*preset), imports);
    EXPECT_EQ(preset->children.front()->kind, appbox::StartupNodeKind::Import);
}

TEST(Unit_StartupTree, ListsFoldersAndExecutables)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFolder(app.Folder(), L"bin");
    MakeFile(app.Folder(), L"app.exe", "EXE");
    MakeFile(app.Folder(), L"upper.EXE", "EXE");
    MakeFile(app.Folder(), L"notes.txt", "TXT");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    tree.EnsureChildren(*import);

    ASSERT_TRUE(import->populated);
    const auto labels = ChildLabels(*import);
    ASSERT_EQ(labels.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(import->children[0]->kind, appbox::StartupNodeKind::Directory);
    EXPECT_EQ(labels[0], L"bin");
    EXPECT_EQ(import->children[1]->kind, appbox::StartupNodeKind::Executable);
    EXPECT_EQ(labels[1], L"app.exe");
    EXPECT_EQ(labels[2], L"upper.EXE");
    EXPECT_EQ(import->children[1]->relative_path, L"app.exe");
    EXPECT_EQ(import->children[0]->parent, import);
}

TEST(Unit_StartupTree, EnsureChildrenIsIdempotent)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    tree.EnsureChildren(*import);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    /* A later change of the host folder is not picked up by the cache. */
    MakeFile(app.Folder(), L"late.exe", "EXE");
    tree.EnsureChildren(*import);

    EXPECT_EQ(import->children.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ChildLabels(*import).front(), L"app.exe");
}

TEST(Unit_StartupTree, SortsFoldersFirstThenNames)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFolder(app.Folder(), L"beta");
    MakeFolder(app.Folder(), L"Alpha");
    MakeFolder(app.Folder(), L"gamma");
    MakeFile(app.Folder(), L"z.exe", "EXE");
    MakeFile(app.Folder(), L"A.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    tree.EnsureChildren(*import);

    const std::vector<std::wstring> expected{ L"Alpha", L"beta", L"gamma", L"A.exe", L"z.exe" };
    EXPECT_EQ(ChildLabels(*import), expected);
}

TEST(Unit_StartupTree, MissingFolderStaysEmpty)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    tree.EnsureChildren(*import);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    /* The folder disappears before it is expanded. */
    std::error_code ec;
    std::filesystem::remove_all(app.Folder(), ec);

    appbox::StartupTree        second(app.Model());
    appbox::StartupNode* const missing = ImportRow(second);
    ASSERT_NE(missing, nullptr);

    second.EnsureChildren(*missing);

    EXPECT_TRUE(missing->populated);
    EXPECT_TRUE(missing->children.empty());
}

TEST(Unit_StartupTree, NestedFoldersKeepBackslashPaths)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(MakeFolder(app.Folder(), L"a"), L"b"), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    appbox::StartupNode* const file = tree.FindNode(*import, L"a\\b\\app.exe");
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->kind, appbox::StartupNodeKind::Executable);
    EXPECT_EQ(file->relative_path, L"a\\b\\app.exe");
    EXPECT_EQ(file->import_name, app.ImportName());
    EXPECT_EQ(file->preset_id, "program_files");
    EXPECT_EQ(file->parent->relative_path, L"a\\b");
}

TEST(Unit_StartupTree, FindNodeRejectsUnknownPath)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    EXPECT_EQ(tree.FindNode(*import, L"missing\\app.exe"), nullptr);
    EXPECT_EQ(tree.FindNode(*import, L"app.exe\\deeper"), nullptr) << "a file has no children";
    EXPECT_NE(tree.FindNode(*import, L"APP.EXE"), nullptr) << "the lookup ignores the case";
}

TEST(Unit_StartupTree, OnlyExecutablesCanBecomeStartupFiles)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFolder(app.Folder(), L"bin");
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    tree.EnsureChildren(*import);

    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(2));
    const appbox::StartupNode& folder = *import->children[0];
    const appbox::StartupNode& file = *import->children[1];

    EXPECT_FALSE(appbox::StartupTree::IsCheckable(folder));
    EXPECT_FALSE(appbox::StartupTree::IsCheckable(*import));
    EXPECT_FALSE(appbox::StartupTree::IsCheckable(*tree.Roots().front()));
    EXPECT_TRUE(appbox::StartupTree::IsCheckable(file));

    EXPECT_FALSE(tree.SetAutoStart(folder, true));
    EXPECT_FALSE(tree.SetAutoStart(*import, true));
    EXPECT_FALSE(tree.HasFiles());

    EXPECT_TRUE(tree.SetAutoStart(file, true));
    EXPECT_TRUE(tree.HasFiles());
    EXPECT_TRUE(tree.Contains(file));
    EXPECT_TRUE(tree.IsAutoStart(file));
}

TEST(Unit_StartupTree, StartupFilesAreIndependent)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"first.exe", "EXE");
    MakeFile(app.Folder(), L"second.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    tree.EnsureChildren(*import);

    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(2));
    const appbox::StartupNode& first = *import->children[0];
    const appbox::StartupNode& second = *import->children[1];

    EXPECT_TRUE(tree.SetAutoStart(first, true));
    EXPECT_TRUE(tree.SetAutoStart(second, true));
    EXPECT_TRUE(tree.SetAutoStart(second, false));

    const auto& files = tree.Files();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(files[0].relative_path, L"first.exe");
    EXPECT_EQ(files[0].import_name, app.ImportName());
    EXPECT_EQ(files[0].preset_id, "program_files");
    EXPECT_EQ(files[1].relative_path, L"second.exe");

    EXPECT_TRUE(tree.Contains(first));
    EXPECT_TRUE(tree.Contains(second));
    EXPECT_TRUE(tree.IsAutoStart(first));
    EXPECT_FALSE(tree.IsAutoStart(second));
}

TEST(Unit_StartupTree, SetAutoStartClearsTheFlag)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    tree.EnsureChildren(*import);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    const appbox::StartupNode& file = *import->children[0];

    ASSERT_TRUE(tree.SetAutoStart(file, true));
    EXPECT_TRUE(tree.SetAutoStart(file, false));

    EXPECT_TRUE(tree.Contains(file));
    EXPECT_FALSE(tree.IsAutoStart(file));
    EXPECT_EQ(tree.Files().size(), static_cast<std::size_t>(1));
}

TEST(Unit_StartupTree, RemoveDropsAStartupFile)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    tree.EnsureChildren(*import);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    const appbox::StartupNode& file = *import->children[0];

    ASSERT_TRUE(tree.SetAutoStart(file, true));
    EXPECT_TRUE(tree.Remove(file));

    EXPECT_FALSE(tree.HasFiles());
    EXPECT_FALSE(tree.Contains(file));
    EXPECT_FALSE(tree.Remove(file)) << "the row is no longer a startup file";
}

TEST(Unit_StartupTree, TriggerDefaultsToTheFileName)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(app.Folder(), L"bin"), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    appbox::StartupNode* const file = tree.FindNode(*import, L"bin\\app.exe");
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(tree.SetAutoStart(*file, true));

    EXPECT_EQ(tree.TriggerOf(*file), L"app");
}

TEST(Unit_StartupTree, SetTriggerRejectsEmptyAndDuplicate)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"first.exe", "EXE");
    MakeFile(app.Folder(), L"second.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    tree.EnsureChildren(*import);

    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(2));
    const appbox::StartupNode& first = *import->children[0];
    const appbox::StartupNode& second = *import->children[1];

    ASSERT_TRUE(tree.SetAutoStart(first, true));
    ASSERT_TRUE(tree.SetAutoStart(second, true));

    std::string error;
    EXPECT_FALSE(tree.SetTrigger(first, L"   ", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(tree.TriggerOf(first), L"first");

    EXPECT_TRUE(tree.SetTrigger(first, L"main", error)) << error;
    EXPECT_EQ(tree.TriggerOf(first), L"main");

    EXPECT_FALSE(tree.SetTrigger(second, L"MAIN", error));
    EXPECT_NE(error.find("used twice"), std::string::npos);
    EXPECT_EQ(tree.TriggerOf(second), L"second");
}

TEST(Unit_StartupTree, SetTriggerRequiresAStartupFile)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    tree.EnsureChildren(*import);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    std::string error;
    EXPECT_FALSE(tree.SetTrigger(*import->children[0], L"main", error));
    EXPECT_FALSE(tree.HasFiles());
}

TEST(Unit_StartupTree, PreselectTakesOverTheList)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(app.Folder(), L"bin"), L"app.exe", "EXE");

    appbox::StartupTree tree(app.Model());

    appbox::StartupFile choice;
    choice.preset_id = "program_files";
    choice.import_name = app.ImportName();
    choice.relative_path = L"BIN\\APP.EXE";
    choice.trigger = L"main";
    choice.auto_start = true;
    tree.Preselect({ choice });

    EXPECT_TRUE(tree.HasFiles());
    ASSERT_EQ(tree.Files().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(tree.Files()[0].trigger, L"main");

    /* The row does not exist yet, the state follows the identifier. */
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);
    EXPECT_FALSE(import->populated) << "the preselect does not expand the tree";

    appbox::StartupNode* const file = tree.FindChoice(choice);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->kind, appbox::StartupNodeKind::Executable);
    EXPECT_TRUE(tree.Contains(*file));
    EXPECT_TRUE(tree.IsAutoStart(*file));
    EXPECT_EQ(tree.TriggerOf(*file), L"main");
    EXPECT_FALSE(tree.Contains(*file->parent));
}

TEST(Unit_StartupTree, TreeListIsAcceptedByModel)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::PackModel          model = app.Model();
    appbox::StartupTree        tree(model);
    appbox::StartupNode* const import = ImportRow(tree);
    ASSERT_NE(import, nullptr);

    appbox::StartupNode* const file = tree.FindNode(*import, L"app.exe");
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(tree.SetAutoStart(*file, true));

    std::string error;
    ASSERT_TRUE(tree.SetTrigger(*file, L"main", error)) << error;
    EXPECT_TRUE(model.SetStartupFiles(tree.Files(), error)) << error;

    EXPECT_TRUE(model.HasStartupFiles());
    EXPECT_TRUE(model.HasAutoStart());
    EXPECT_EQ(model.StartupFiles()[0].relative_path, L"app.exe");
    EXPECT_EQ(model.StartupFiles()[0].trigger, L"main");
}
