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
    appbox::PackModel& Model()
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
 * @brief Get the container row of a tree.
 * @param[in] tree Tree to inspect.
 * @return The container row, null when the tree has none.
 */
appbox::StartupNode* Container(appbox::StartupTree& tree)
{
    return tree.Roots().empty() ? nullptr : tree.Roots().front().get();
}

/**
 * @brief Find the first row with a label below a row.
 * @param[in] node Row to search below.
 * @param[in] label Label to look for.
 * @return The matching row, null when the subtree has none.
 */
appbox::StartupNode* FindLabel(appbox::StartupNode& node, const std::wstring& label)
{
    for (const auto& child : node.children)
    {
        if (child->label == label)
        {
            return child.get();
        }

        appbox::StartupNode* const found = FindLabel(*child, label);
        if (found != nullptr)
        {
            return found;
        }
    }
    return nullptr;
}

/**
 * @brief Find the row of an imported folder below a preset.
 * @param[in] tree Tree to search.
 * @param[in] preset_label Label of the preset directory.
 * @param[in] import_name Name of the imported folder.
 * @return The import row, null when the tree has none.
 */
appbox::StartupNode* ImportRow(appbox::StartupTree& tree, const std::wstring& preset_label,
                               const std::wstring& import_name)
{
    appbox::StartupNode* const container = Container(tree);
    if (container == nullptr)
    {
        return nullptr;
    }

    appbox::StartupNode* const preset = FindLabel(*container, preset_label);
    return preset == nullptr ? nullptr : FindLabel(*preset, import_name);
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

/**
 * @brief The container is the root of the tree and holds the top level preset
 *        directories, which is the shape of the filesystem workspace.
 */
TEST(Unit_StartupTree, BuildsTheContainerAndTheTopLevelPresets)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree tree(app.Model());

    ASSERT_EQ(tree.Roots().size(), static_cast<std::size_t>(1));
    const appbox::StartupNode& container = *tree.Roots().front();
    EXPECT_EQ(container.kind, appbox::StartupNodeKind::Container);
    EXPECT_EQ(container.label, L"Sandbox Filesystem");
    EXPECT_EQ(container.parent, nullptr);
    EXPECT_TRUE(container.preset_id.empty());
    EXPECT_TRUE(container.host_path.empty());

    /*
     * Only the preset which holds a startable file is offered: the other top
     * level preset directories of the packer are hidden.
     */
    EXPECT_EQ(ChildLabels(container), std::vector<std::wstring>({ L"Program Files" }));

    /*
     * A preset directory holds the nested presets, the imported folders and the
     * folders the user created; the host content of the preset folder itself is
     * never listed.
     */
    const appbox::StartupNode& preset = *container.children.front();
    EXPECT_EQ(preset.kind, appbox::StartupNodeKind::Preset);
    EXPECT_EQ(preset.preset_id, "program_files");
    EXPECT_EQ(preset.host_path, appbox::PresetDirectories().front().real_path);
    for (const auto& child : preset.children)
    {
        EXPECT_TRUE(child->kind == appbox::StartupNodeKind::Preset || child->kind == appbox::StartupNodeKind::Import ||
                    child->kind == appbox::StartupNodeKind::VirtualDirectory)
            << child->label;
    }
}

/**
 * @brief A nested preset directory hangs below the preset which holds it, so
 *        the tree follows the table of the preset directories.
 */
TEST(Unit_StartupTree, NestsThePresetDirectories)
{
    TempDir     folder;
    std::string error;

    appbox::PackModel model;
    ASSERT_TRUE(model.ImportFolder("documents", folder.Get().wstring(), error)) << error;
    const auto import_name = folder.Get().filename().wstring();
    MakeFile(folder.Get(), L"setup.cmd", "CMD");
    ASSERT_TRUE(model.AddStartupFile("documents", import_name, L"setup.cmd", true, error)) << error;

    appbox::StartupTree tree(model);

    const appbox::StartupNode& container = *tree.Roots().front();
    ASSERT_EQ(ChildLabels(container), std::vector<std::wstring>({ L"Current User Directory" }));

    const auto& profile = *container.children.front();
    EXPECT_EQ(profile.preset_id, "user_profile");
    ASSERT_EQ(ChildLabels(profile), std::vector<std::wstring>({ L"Documents" }));

    const auto& documents = *profile.children.front();
    EXPECT_EQ(documents.preset_id, "documents");
    ASSERT_EQ(ChildLabels(documents), std::vector<std::wstring>({ import_name }));
    EXPECT_EQ(documents.children.front()->kind, appbox::StartupNodeKind::Import);
}

/**
 * @brief Only the files the launcher can start are listed, and a folder which
 *        holds none of them is hidden together with everything above it.
 */
TEST(Unit_StartupTree, ListsOnlyStartableFiles)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFolder(app.Folder(), L"bin");
    MakeFolder(app.Folder(), L"docs");
    MakeFile(app.Folder(), L"app.exe", "EXE");
    MakeFile(app.Folder(), L"run.bat", "BAT");
    MakeFile(app.Folder(), L"build.cmd", "CMD");
    MakeFile(app.Folder(), L"deploy.ps1", "PS1");
    MakeFile(app.Folder(), L"notes.txt", "TXT");
    MakeFile(app.Folder(), L"library.dll", "DLL");
    MakeFile(app.Folder(), L"docs\\readme.md", "MD");
    MakeFile(MakeFolder(app.Folder(), L"docs\\deep"), L"more.txt", "TXT");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    /*
     * The folders of the host come first and are ordered by name, then the
     * startable files of the folder follow; `bin` holds no startable file and
     * is hidden.
     */
    const std::vector<std::wstring> expected{ L"app.exe", L"build.cmd", L"deploy.ps1", L"run.bat" };
    EXPECT_EQ(ChildLabels(*import), expected);

    for (const auto& child : import->children)
    {
        EXPECT_EQ(child->kind, appbox::StartupNodeKind::Executable);
        EXPECT_EQ(child->import_name, app.ImportName());
        EXPECT_EQ(child->relative_path, child->label);
        EXPECT_TRUE(appbox::StartupTree::IsCheckable(*child));
    }
}

/**
 * @brief A folder of the host which holds a startable file below it is kept,
 *        and the folders of the way to it as well.
 */
TEST(Unit_StartupTree, KeepsTheFoldersOnTheWayToAStartableFile)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(app.Folder(), L"bin"), L"notes.txt", "TXT");
    MakeFile(MakeFolder(MakeFolder(app.Folder(), L"bin"), L"deep"), L"tool.bat", "BAT");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    ASSERT_EQ(ChildLabels(*import), std::vector<std::wstring>({ L"bin" }));

    const auto& bin = *import->children.front();
    EXPECT_EQ(bin.kind, appbox::StartupNodeKind::Directory);
    EXPECT_EQ(bin.relative_path, L"bin");
    EXPECT_EQ(bin.import_name, app.ImportName());

    ASSERT_EQ(ChildLabels(bin), std::vector<std::wstring>({ L"deep" }));
    EXPECT_EQ(bin.children.front()->relative_path, L"bin\\deep");

    const auto& deep = *bin.children.front();
    ASSERT_EQ(ChildLabels(deep), std::vector<std::wstring>({ L"tool.bat" }));
    EXPECT_EQ(deep.children.front()->kind, appbox::StartupNodeKind::Executable);
    EXPECT_EQ(deep.children.front()->relative_path, L"bin\\deep\\tool.bat");
}

/**
 * @brief The folders the user created and the files which were imported into
 *        them are part of the tree, exactly like in the filesystem workspace.
 */
TEST(Unit_StartupTree, ListsFoldersTheUserCreated)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    std::string error;
    ASSERT_TRUE(app.Model().CreateVirtualDirectory("program_files", app.ImportName(), L"logs", error)) << error;

    TempDir    source_dir;
    const auto source = MakeFile(source_dir.Get(), L"run.bat", "BAT");
    ASSERT_TRUE(app.Model().ImportFiles("program_files", app.ImportName() + L"\\logs", { source.wstring() }, error))
        << error;

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    ASSERT_EQ(ChildLabels(*import), std::vector<std::wstring>({ L"logs" }));

    const auto& logs = *import->children.front();
    EXPECT_EQ(logs.kind, appbox::StartupNodeKind::VirtualDirectory);
    EXPECT_EQ(logs.virtual_dir, app.ImportName() + L"\\logs");
    EXPECT_TRUE(logs.host_path.empty()) << "a folder the user created has no host counterpart";

    ASSERT_EQ(ChildLabels(logs), std::vector<std::wstring>({ L"run.bat" }));

    const auto& file = *logs.children.front();
    EXPECT_EQ(file.kind, appbox::StartupNodeKind::Executable);
    EXPECT_EQ(file.import_name, app.ImportName());
    EXPECT_EQ(file.relative_path, L"logs\\run.bat");
    EXPECT_EQ(file.host_path, source.wstring());

    /* The row is a startup file the model accepts. */
    ASSERT_TRUE(tree.SetAutoStart(file, true));
    ASSERT_TRUE(app.Model().SetStartupFiles(tree.Files(), error)) << error;
    EXPECT_EQ(app.Model().StartupFiles()[0].relative_path, L"logs\\run.bat");
}

/**
 * @brief A file which was imported on its own is listed, and only while it can
 *        be started.
 */
TEST(Unit_StartupTree, ListsImportedFiles)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    TempDir    source_dir;
    const auto script = MakeFile(source_dir.Get(), L"tool.bat", "BAT");
    const auto data = MakeFile(source_dir.Get(), L"settings.ini", "INI");

    std::string error;
    ASSERT_TRUE(app.Model().ImportFiles("program_files", app.ImportName(), { script.wstring(), data.wstring() }, error))
        << error;

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    ASSERT_EQ(ChildLabels(*import), std::vector<std::wstring>({ L"tool.bat" }));
    EXPECT_EQ(import->children.front()->relative_path, L"tool.bat");
    EXPECT_EQ(import->children.front()->host_path, script.wstring());
}

/**
 * @brief A startup file whose host file is gone keeps its place in the tree, so
 *        the user sees it and can drop it.
 */
TEST(Unit_StartupTree, KeepsAStartupFileWhoseHostFileIsGone)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    const auto file = MakeFile(MakeFolder(app.Folder(), L"bin"), L"app.exe", "EXE");
    MakeFile(app.Folder(), L"bin\\notes.txt", "TXT");

    std::string error;
    ASSERT_TRUE(app.Model().AddStartupFile("program_files", app.ImportName(), L"bin\\app.exe", true, error)) << error;

    /* The file disappears before the tree is built. */
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::remove(file, ec));
    ASSERT_FALSE(ec);

    appbox::StartupTree tree(app.Model());

    appbox::StartupNode* const row = tree.FindChoice(app.Model().StartupFiles().front());
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->kind, appbox::StartupNodeKind::Executable);
    EXPECT_EQ(row->label, L"app.exe");
    EXPECT_EQ(row->relative_path, L"bin\\app.exe");
    EXPECT_TRUE(tree.Contains(*row));
    EXPECT_TRUE(tree.IsAutoStart(*row));

    /* The folder was pruned with the host content and kept for the file. */
    ASSERT_NE(row->parent, nullptr);
    EXPECT_EQ(row->parent->kind, appbox::StartupNodeKind::Directory);
    EXPECT_EQ(row->parent->label, L"bin");
    EXPECT_EQ(ChildLabels(*row->parent), std::vector<std::wstring>({ L"app.exe" }));
}

/**
 * @brief A project can name a startup file of a machine where the packaged
 *        application is not installed, so the whole path is offered.
 */
TEST(Unit_StartupTree, KeepsThePathOfAFileWhichIsNotInstalled)
{
    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\appbox-startuptree-missing\\MyApp", error))
        << error;
    ASSERT_TRUE(model.RestoreStartupFile("program_files", L"MyApp", L"bin\\deploy.ps1", L"deploy", true, error))
        << error;

    appbox::StartupTree tree(model);

    appbox::StartupNode* const row = tree.FindChoice(model.StartupFiles().front());
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->label, L"deploy.ps1");
    EXPECT_EQ(row->relative_path, L"bin\\deploy.ps1");
    EXPECT_EQ(row->kind, appbox::StartupNodeKind::Executable);
    EXPECT_TRUE(tree.Contains(*row));

    ASSERT_NE(row->parent, nullptr);
    EXPECT_EQ(row->parent->label, L"bin");
    ASSERT_NE(row->parent->parent, nullptr);
    EXPECT_EQ(row->parent->parent->label, L"MyApp");
    EXPECT_EQ(row->parent->parent->kind, appbox::StartupNodeKind::Import);
    ASSERT_NE(row->parent->parent->parent, nullptr);
    EXPECT_EQ(row->parent->parent->parent->label, L"Program Files");
}

/**
 * @brief A tree of a model without an import holds the container only.
 */
TEST(Unit_StartupTree, TheEmptyModelShowsTheContainerOnly)
{
    appbox::PackModel   model;
    appbox::StartupTree tree(model);

    ASSERT_EQ(tree.Roots().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(tree.Roots().front()->kind, appbox::StartupNodeKind::Container);
    EXPECT_TRUE(tree.Roots().front()->children.empty());
    EXPECT_FALSE(tree.HasFiles());
}

/**
 * @brief Every folder row of the tree is reported for the expansion of the
 *        dialog, so it opens with the whole tree in view.
 */
TEST(Unit_StartupTree, ExpandableRowsHoldTheFoldersOfTheTree)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(app.Folder(), L"bin"), L"app.exe", "EXE");
    MakeFile(app.Folder(), L"bin\\deep\\tool.bat", "BAT");
    MakeFile(app.Folder(), L"run.bat", "BAT");

    std::string error;
    ASSERT_TRUE(app.Model().CreateVirtualDirectory("program_files", app.ImportName(), L"logs", error)) << error;

    TempDir    source_dir;
    const auto script = MakeFile(source_dir.Get(), L"start.cmd", "CMD");
    ASSERT_TRUE(app.Model().ImportFiles("program_files", app.ImportName() + L"\\logs", { script.wstring() }, error))
        << error;

    appbox::StartupTree tree(app.Model());

    const std::vector<const appbox::StartupNode*> rows = tree.ExpandableRows();

    /*
     * The rows are reported in the order of the tree, which starts at the
     * container row and walks the folders down to the files below them.
     */
    std::vector<std::wstring> labels;
    for (const appbox::StartupNode* const row : rows)
    {
        ASSERT_NE(row, nullptr);
        labels.push_back(row->label);
    }

    const std::vector<std::wstring> expected{
        L"Sandbox Filesystem", L"Program Files", app.ImportName(), L"bin", L"deep", L"logs"
    };
    EXPECT_EQ(labels, expected);

    /*
     * The control expands a row which holds children only, so the rows which
     * name a startable file are never part of the list.
     */
    for (const appbox::StartupNode* const row : rows)
    {
        EXPECT_FALSE(row->children.empty()) << row->label;
        EXPECT_FALSE(appbox::StartupTree::IsCheckable(*row)) << row->label;
    }
}

/**
 * @brief A tree without a row to expand reports none, which is the state of a
 *        model without an import.
 */
TEST(Unit_StartupTree, ExpandableRowsOfAnEmptyTreeAreEmpty)
{
    appbox::PackModel   model;
    appbox::StartupTree tree(model);

    EXPECT_TRUE(tree.ExpandableRows().empty()) << "the container row holds no child";
}

/**
 * @brief The rows of a folder are ordered folders first and then by name, which
 *        keeps the folders the user created next to the folders of the host.
 */
TEST(Unit_StartupTree, SortsFoldersFirstThenNames)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(app.Folder(), L"Alpha"), L"a.exe", "EXE");
    MakeFile(MakeFolder(app.Folder(), L"beta"), L"b.bat", "BAT");
    MakeFile(MakeFolder(app.Folder(), L"gamma"), L"g.exe", "EXE");
    MakeFile(app.Folder(), L"z.exe", "EXE");
    MakeFile(app.Folder(), L"A.exe", "EXE");

    std::string error;
    ASSERT_TRUE(app.Model().CreateVirtualDirectory("program_files", app.ImportName(), L"New Folder", error)) << error;

    TempDir    source_dir;
    const auto source = MakeFile(source_dir.Get(), L"tool.cmd", "CMD");
    ASSERT_TRUE(
        app.Model().ImportFiles("program_files", app.ImportName() + L"\\New Folder", { source.wstring() }, error))
        << error;

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    /*
     * The folders of the host and the folder the user created are ordered by
     * name ignoring the case, and the startable files of the folder follow
     * them.
     */
    const std::vector<std::wstring> expected{ L"Alpha", L"beta", L"gamma", L"New Folder", L"A.exe", L"z.exe" };
    EXPECT_EQ(ChildLabels(*import), expected);
}

/**
 * @brief Only the rows which name a startable file can be a startup file.
 */
TEST(Unit_StartupTree, OnlyStartableFilesCanBecomeStartupFiles)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(MakeFolder(app.Folder(), L"bin"), L"tool.bat", "BAT");
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(2));

    const appbox::StartupNode& folder = *import->children[0];
    const appbox::StartupNode& file = *import->children[1];

    EXPECT_EQ(folder.kind, appbox::StartupNodeKind::Directory);
    EXPECT_EQ(file.kind, appbox::StartupNodeKind::Executable);
    EXPECT_FALSE(appbox::StartupTree::IsCheckable(folder));
    EXPECT_FALSE(appbox::StartupTree::IsCheckable(*import));
    EXPECT_FALSE(appbox::StartupTree::IsCheckable(*Container(tree)));
    EXPECT_FALSE(appbox::StartupTree::IsCheckable(*import->parent));
    EXPECT_TRUE(appbox::StartupTree::IsCheckable(file));

    EXPECT_FALSE(tree.SetAutoStart(folder, true));
    EXPECT_FALSE(tree.SetAutoStart(*import, true));
    EXPECT_FALSE(tree.HasFiles());
    EXPECT_FALSE(tree.Contains(folder));
    EXPECT_FALSE(tree.Remove(folder));

    std::string error;
    EXPECT_FALSE(tree.SetTrigger(folder, L"main", error));

    EXPECT_TRUE(tree.SetAutoStart(file, true));
    EXPECT_TRUE(tree.HasFiles());
    EXPECT_TRUE(tree.Contains(file));
    EXPECT_TRUE(tree.IsAutoStart(file));
}

/**
 * @brief Every startable extension is offered and can join the list.
 */
TEST(Unit_StartupTree, OffersEveryStartableExtension)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"app.exe", "EXE");
    MakeFile(app.Folder(), L"run.bat", "BAT");
    MakeFile(app.Folder(), L"build.cmd", "CMD");
    MakeFile(app.Folder(), L"deploy.ps1", "PS1");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(4));

    for (const auto& child : import->children)
    {
        EXPECT_TRUE(tree.SetAutoStart(*child, true)) << child->label;
    }

    const auto& files = tree.Files();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(files[0].trigger, L"app");
    EXPECT_EQ(files[1].trigger, L"build");
    EXPECT_EQ(files[2].trigger, L"deploy");
    EXPECT_EQ(files[3].trigger, L"run");

    for (const auto& file : files)
    {
        EXPECT_EQ(file.preset_id, "program_files");
        EXPECT_EQ(file.import_name, app.ImportName());
    }
}

/**
 * @brief The rows of the tree can become startup files one by one.
 */
TEST(Unit_StartupTree, StartupFilesAreIndependent)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"first.exe", "EXE");
    MakeFile(app.Folder(), L"second.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
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

/**
 * @brief Clearing the auto start flag keeps the file in the list.
 */
TEST(Unit_StartupTree, SetAutoStartClearsTheFlag)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    const appbox::StartupNode& file = *import->children[0];

    ASSERT_TRUE(tree.SetAutoStart(file, true));
    EXPECT_TRUE(tree.SetAutoStart(file, false));

    EXPECT_TRUE(tree.Contains(file));
    EXPECT_FALSE(tree.IsAutoStart(file));
    EXPECT_EQ(tree.Files().size(), static_cast<std::size_t>(1));
}

/**
 * @brief A row can be dropped from the list again.
 */
TEST(Unit_StartupTree, RemoveDropsAStartupFile)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    const appbox::StartupNode& file = *import->children[0];

    ASSERT_TRUE(tree.SetAutoStart(file, true));
    EXPECT_TRUE(tree.Remove(file));

    EXPECT_FALSE(tree.HasFiles());
    EXPECT_FALSE(tree.Contains(file));
    EXPECT_FALSE(tree.Remove(file)) << "the row is no longer a startup file";
}

/**
 * @brief The trigger of a new startup file is the file name without its
 *        extension.
 */
TEST(Unit_StartupTree, TriggerDefaultsToTheFileName)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(MakeFolder(app.Folder(), L"bin"), L"deploy.ps1", "PS1");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    appbox::StartupNode* const file = tree.FindNode(*import, L"bin\\deploy.ps1");
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(tree.SetAutoStart(*file, true));

    EXPECT_EQ(tree.TriggerOf(*file), L"deploy");
}

/**
 * @brief The trigger of a startup file has to be unique and not empty.
 */
TEST(Unit_StartupTree, SetTriggerRejectsEmptyAndDuplicate)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());

    MakeFile(app.Folder(), L"first.exe", "EXE");
    MakeFile(app.Folder(), L"second.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
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

/**
 * @brief Only a startup file carries a trigger.
 */
TEST(Unit_StartupTree, SetTriggerRequiresAStartupFile)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);
    ASSERT_EQ(import->children.size(), static_cast<std::size_t>(1));

    std::string error;
    EXPECT_FALSE(tree.SetTrigger(*import->children[0], L"main", error));
    EXPECT_FALSE(tree.HasFiles());
}

/**
 * @brief The tree takes over the startup file list of the model, so the dialog
 *        shows the files of a session which was loaded from a project file.
 */
TEST(Unit_StartupTree, TakesOverTheListOfTheModel)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(MakeFolder(app.Folder(), L"bin"), L"app.exe", "EXE");

    std::string error;
    ASSERT_TRUE(app.Model().AddStartupFile("program_files", app.ImportName(), L"bin\\app.exe", false, error)) << error;

    appbox::StartupTree tree(app.Model());

    ASSERT_TRUE(tree.HasFiles());
    ASSERT_EQ(tree.Files().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(tree.Files()[0].trigger, L"app");
    EXPECT_FALSE(tree.Files()[0].auto_start);

    appbox::StartupNode* const file = tree.FindChoice(tree.Files()[0]);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->kind, appbox::StartupNodeKind::Executable);
    EXPECT_TRUE(tree.Contains(*file));
    EXPECT_FALSE(tree.IsAutoStart(*file));
    EXPECT_EQ(tree.TriggerOf(*file), L"app");
    EXPECT_FALSE(tree.Contains(*file->parent)) << "a folder is not a startup file";
}

/**
 * @brief The list of the tree is accepted by the model, which keeps the dialog
 *        and the pack run in step.
 */
TEST(Unit_StartupTree, TreeListIsAcceptedByModel)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    appbox::StartupNode* const file = tree.FindNode(*import, L"app.exe");
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(tree.SetAutoStart(*file, true));

    std::string error;
    ASSERT_TRUE(tree.SetTrigger(*file, L"main", error)) << error;
    EXPECT_TRUE(app.Model().SetStartupFiles(tree.Files(), error)) << error;

    EXPECT_TRUE(app.Model().HasStartupFiles());
    EXPECT_TRUE(app.Model().HasAutoStart());
    EXPECT_EQ(app.Model().StartupFiles()[0].relative_path, L"app.exe");
    EXPECT_EQ(app.Model().StartupFiles()[0].trigger, L"main");
}

/**
 * @brief The lookup of a path below an import root rejects what the tree does
 *        not hold.
 */
TEST(Unit_StartupTree, FindNodeRejectsUnknownPath)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree        tree(app.Model());
    appbox::StartupNode* const import = ImportRow(tree, L"Program Files", app.ImportName());
    ASSERT_NE(import, nullptr);

    EXPECT_EQ(tree.FindNode(*import, L"missing\\app.exe"), nullptr);
    EXPECT_EQ(tree.FindNode(*import, L"app.exe\\deeper"), nullptr) << "a file has no children";
    EXPECT_NE(tree.FindNode(*import, L"APP.EXE"), nullptr) << "the lookup ignores the case";
    EXPECT_EQ(tree.FindNode(*import, L"app.exe"), import->children.front().get());
}

/**
 * @brief A startup file of a preset the tree does not hold has no row.
 */
TEST(Unit_StartupTree, FindChoiceRejectsAnUnknownPreset)
{
    ImportedApp app;
    ASSERT_TRUE(app.Imported());
    MakeFile(app.Folder(), L"app.exe", "EXE");

    appbox::StartupTree tree(app.Model());

    appbox::StartupFile file;
    file.preset_id = "windows";
    file.import_name = app.ImportName();
    file.relative_path = L"app.exe";

    EXPECT_EQ(tree.FindChoice(file), nullptr);
}
