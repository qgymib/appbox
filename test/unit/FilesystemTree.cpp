#include <gtest/gtest.h>
#include "src/core/FilesystemTree.hpp"
#include <chrono>
#include <cstddef>
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
        path_ = base / (L"appbox-fstree-" + UniqueFragment());
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
    if (!content.empty())
    {
        fwrite(content.data(), 1, content.size(), handle);
    }
    fclose(handle);
    return file;
}

/**
 * @brief Get the names of the folders of a result.
 * @param[in] folders Folders to describe.
 * @return The display names in the order of the result.
 */
std::vector<std::wstring> NamesOf(const std::vector<appbox::TreeFolder>& folders)
{
    std::vector<std::wstring> names;
    names.reserve(folders.size());
    for (const auto& folder : folders)
    {
        names.push_back(folder.display_name);
    }
    return names;
}

/**
 * @brief Get the depth of one entry of a subtree result.
 *
 * The depth is the number of levels between the entry and the node the walk
 * started at, which is one for a direct child. The parent indices of the
 * result are followed until the root parent is reached.
 *
 * @param[in] entries Result of SubtreeFoldersOf().
 * @param[in] index Index of the entry to describe.
 * @return The depth of the entry.
 */
std::size_t DepthOf(const std::vector<appbox::SubtreeFolder>& entries, std::size_t index)
{
    std::size_t depth = 1;
    for (auto parent = entries[index].parent; parent != appbox::kTreeRootParent; parent = entries[parent].parent)
    {
        ++depth;
    }
    return depth;
}

/**
 * @brief Create the location of the import root of a folder imported below a preset.
 * @param[in] preset_id Identifier of the preset directory.
 * @param[in] import_name Name of the imported folder.
 * @return The location of the import root.
 */
appbox::TreeFolder ImportRoot(const std::string& preset_id, const std::wstring& import_name)
{
    appbox::TreeFolder node;
    node.preset_id = preset_id;
    node.import_name = import_name;
    return node;
}

} // namespace

TEST(Unit_FilesystemTree, ChildFoldersListTheHostFoldersSortedAndTheCreatedOnesAfterThem)
{
    TempDir temp;
    MakeFolder(temp.Get(), L"zeta");
    MakeFolder(temp.Get(), L"alpha");
    MakeFile(temp.Get(), L"readme.txt", "TXT");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;

    const auto import_name = temp.Get().filename().wstring();
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name, L"logs", error)) << error;

    const auto children = appbox::ChildFoldersOf(model, ImportRoot("program_files", import_name));
    ASSERT_EQ(children.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(NamesOf(children), (std::vector<std::wstring>{ L"alpha", L"zeta", L"logs" }));

    EXPECT_EQ(children[0].preset_id, "program_files");
    EXPECT_EQ(children[0].import_name, import_name);
    EXPECT_EQ(children[0].relative_dir, L"alpha");
    EXPECT_TRUE(children[0].virtual_dir.empty());

    /* The folder the user created belongs to the layer and not to the import. */
    EXPECT_TRUE(children[2].import_name.empty());
    EXPECT_EQ(children[2].relative_dir, std::wstring());
    EXPECT_EQ(children[2].virtual_dir, import_name + L"\\logs");
}

TEST(Unit_FilesystemTree, ChildFoldersListNothingForTheContainerAndAPresetDirectory)
{
    TempDir temp;
    MakeFolder(temp.Get(), L"alpha");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"", L"logs", error)) << error;

    EXPECT_TRUE(appbox::ChildFoldersOf(model, appbox::TreeFolder()).empty());

    appbox::TreeFolder preset;
    preset.preset_id = "program_files";
    EXPECT_TRUE(appbox::ChildFoldersOf(model, preset).empty());
}

TEST(Unit_FilesystemTree, ChildFoldersKeepTheCreatedFoldersWhenTheHostFolderIsGone)
{
    TempDir temp;
    MakeFolder(temp.Get(), L"alpha");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    const auto import_name = temp.Get().filename().wstring();
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name, L"logs", error)) << error;

    std::error_code ec;
    std::filesystem::remove_all(temp.Get(), ec);
    ASSERT_FALSE(ec);

    const auto children = appbox::ChildFoldersOf(model, ImportRoot("program_files", import_name));
    ASSERT_EQ(children.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(children[0].display_name, L"logs");
    EXPECT_EQ(children[0].virtual_dir, import_name + L"\\logs");
}

TEST(Unit_FilesystemTree, ChildFoldersOfACreatedFolderListTheCreatedFoldersBelowIt)
{
    TempDir temp;

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    const auto import_name = temp.Get().filename().wstring();
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name, L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name + L"\\logs", L"deep", error)) << error;

    appbox::TreeFolder logs;
    logs.preset_id = "program_files";
    logs.virtual_dir = import_name + L"\\logs";

    const auto children = appbox::ChildFoldersOf(model, logs);
    ASSERT_EQ(children.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(children[0].display_name, L"deep");
    EXPECT_EQ(children[0].virtual_dir, import_name + L"\\logs\\deep");
    EXPECT_TRUE(children[0].import_name.empty());
}

TEST(Unit_FilesystemTree, SubtreeWalksDepthFirstAndNamesTheParentOfEveryFolder)
{
    TempDir temp;
    MakeFolder(temp.Get(), L"alpha");
    MakeFolder(MakeFolder(temp.Get(), L"beta"), L"gamma");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    const auto import_name = temp.Get().filename().wstring();
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name, L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", import_name + L"\\logs", L"deep", error)) << error;

    const auto entries = appbox::SubtreeFoldersOf(model, ImportRoot("program_files", import_name));
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(5));

    /* The host content comes first, the folders the user created follow. */
    std::vector<std::wstring> names;
    for (const auto& entry : entries)
    {
        names.push_back(entry.folder.display_name);
    }
    EXPECT_EQ(names, (std::vector<std::wstring>{ L"alpha", L"beta", L"gamma", L"logs", L"deep" }));

    EXPECT_EQ(entries[0].parent, appbox::kTreeRootParent);
    EXPECT_EQ(entries[1].parent, appbox::kTreeRootParent);
    EXPECT_EQ(entries[2].parent, static_cast<std::size_t>(1));
    EXPECT_EQ(entries[3].parent, appbox::kTreeRootParent);
    EXPECT_EQ(entries[4].parent, static_cast<std::size_t>(3));

    EXPECT_EQ(entries[2].folder.relative_dir, L"beta\\gamma");
    EXPECT_EQ(entries[4].folder.virtual_dir, import_name + L"\\logs\\deep");
    EXPECT_EQ(DepthOf(entries, 4), static_cast<std::size_t>(2));
}

TEST(Unit_FilesystemTree, SubtreeListsNothingForTheContainerAndAPresetDirectory)
{
    TempDir temp;
    MakeFolder(temp.Get(), L"alpha");

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;

    EXPECT_TRUE(appbox::SubtreeFoldersOf(model, appbox::TreeFolder()).empty());

    appbox::TreeFolder preset;
    preset.preset_id = "program_files";
    EXPECT_TRUE(appbox::SubtreeFoldersOf(model, preset).empty());
}

TEST(Unit_FilesystemTree, SubtreeHonoursTheDepthLimit)
{
    TempDir temp;

    /* A chain of seventy levels is deeper than the default limit of the walk. */
    auto current = temp.Get();
    for (int level = 1; level <= 70; ++level)
    {
        current = MakeFolder(current, L"d");
    }

    appbox::PackModel model;
    std::string       error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    const auto import_name = temp.Get().filename().wstring();

    const auto entries = appbox::SubtreeFoldersOf(model, ImportRoot("program_files", import_name));
    ASSERT_EQ(entries.size(), appbox::kMaxTreeExpandDepth);
    EXPECT_EQ(DepthOf(entries, entries.size() - 1), appbox::kMaxTreeExpandDepth);

    /* A limit of zero reports no folder, a limit of one the direct children only. */
    EXPECT_TRUE(appbox::SubtreeFoldersOf(model, ImportRoot("program_files", import_name), 0).empty());

    const auto one_level = appbox::SubtreeFoldersOf(model, ImportRoot("program_files", import_name), 1);
    ASSERT_EQ(one_level.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(one_level[0].parent, appbox::kTreeRootParent);
}
