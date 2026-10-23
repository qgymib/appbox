#ifndef APPBOX_PACKER_CORE_FILESYSTEM_TREE_HPP
#define APPBOX_PACKER_CORE_FILESYSTEM_TREE_HPP

#include "PackModel.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Parent index of a folder which hangs directly below the queried node.
 */
inline constexpr std::size_t kTreeRootParent = static_cast<std::size_t>(-1);

/**
 * @brief Deepest level the recursive collection of a subtree descends to.
 *
 * A host folder which was imported by mistake (a system folder, for instance)
 * can hold a huge tree, so the recursive walk stops at this depth instead of
 * following the host as deep as it goes. The limit counts the levels below the
 * node the walk starts at and is not a limit on the width of a level.
 */
inline constexpr std::size_t kMaxTreeExpandDepth = 64;

/**
 * @brief Location of one folder inside the filesystem tree.
 *
 * The fields mirror the client data of the tree items of the filesystem
 * workspace: the container leaves all of them empty, a preset directory
 * carries its identifier only, an imported folder adds its name, a folder
 * below an import adds its path relative to the import root, and a folder the
 * user created adds its path relative to the preset directory.
 */
struct TreeFolder
{
    /**
     * @brief Identifier of the preset directory, empty for the container.
     */
    std::string preset_id;

    /**
     * @brief Name of the imported folder, empty for preset nodes and for the
     *        folders the user created.
     */
    std::wstring import_name;

    /**
     * @brief Folder path relative to the import root, empty for import roots.
     */
    std::wstring relative_dir;

    /**
     * @brief Path of a folder the user created, relative to the preset
     *        directory, empty for every other node.
     */
    std::wstring virtual_dir;

    /**
     * @brief Name shown in the tree.
     *
     * The field describes a child which ChildFoldersOf() returned; it is
     * ignored when the struct locates the node a query starts at.
     */
    std::wstring display_name;
};

/**
 * @brief One folder below a node, with the index of its parent in the result.
 */
struct SubtreeFolder
{
    /**
     * @brief Location of the folder.
     */
    TreeFolder folder;

    /**
     * @brief Index of the parent inside the result, `kTreeRootParent` when the
     *        folder hangs directly below the queried node.
     */
    std::size_t parent = kTreeRootParent;
};

/**
 * @brief Get the folders which hang directly below one node of the tree.
 *
 * The host subfolders of the folder come first, sorted by name, because they
 * come from the imported folder, and the folders the user created below the
 * folder follow in the order they were created in. The order is the order the
 * tree shows, so a caller which builds items from the result appends them the
 * way a manual expansion of the node does.
 *
 * The container and the preset directories are not populated: their children
 * belong to the fixed tree of the view, which the workspace builds from the
 * preset table and from the imports of a preset directory. A host folder which
 * cannot be read contributes no subfolder, so a folder the user created below
 * it is still reported.
 *
 * @param[in] model The pack model holding the view.
 * @param[in] node Location of the node whose children are listed.
 * @return The child folders, empty for the container, for a preset directory
 *         and for a folder without subfolders.
 */
std::vector<TreeFolder> ChildFoldersOf(const PackModel& model, const TreeFolder& node);

/**
 * @brief Get every folder below one node of the tree, depth first.
 *
 * The result is a flat list in the order a depth first walk visits the
 * folders: a folder comes before its children and the children of a folder
 * keep the order ChildFoldersOf() reports. The parent of a folder is the index
 * of the folder it hangs below, so a caller which builds one item per entry
 * can attach every item to the item of its parent without walking the levels
 * of the tree itself.
 *
 * The walk stops below the level `max_depth`, which counts the levels below
 * `node`: the direct children of the node are on level one. The limit is a
 * guard against a host folder which was imported by mistake, see
 * `kMaxTreeExpandDepth`.
 *
 * @param[in] model The pack model holding the view.
 * @param[in] node Location of the node whose subtree is collected.
 * @param[in] max_depth Deepest level the walk descends to, zero for no folder
 *            at all.
 * @return The folders of the subtree, empty for the container, for a preset
 *         directory and for a folder without subfolders.
 */
std::vector<SubtreeFolder> SubtreeFoldersOf(const PackModel& model, const TreeFolder& node,
                                            std::size_t max_depth = kMaxTreeExpandDepth);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_FILESYSTEM_TREE_HPP
