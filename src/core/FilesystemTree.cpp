#include "FilesystemTree.hpp"
#include "FilesystemIsolationModel.hpp"
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace
{

/**
 * @brief Get the name of an entry from a path relative to a preset directory.
 * @param[in] path Path of the entry.
 * @return The last component of the path, the whole path when it holds no
 *         separator.
 */
std::wstring LeafName(const std::wstring& path)
{
    return path.substr(path.find_last_of(L'\\') + 1);
}

/**
 * @brief Get the directory a node names, relative to its preset directory.
 *
 * The container and a preset directory name the layer root, which is the empty
 * path; an imported folder and a folder below it name the path below the
 * import root, and a folder the user created names the path it was stored
 * under. The directory is what the folders the user created below the node are
 * looked up by.
 *
 * @param[in] node Location of the node.
 * @return The path relative to the preset directory.
 */
std::wstring NodeDirectoryPath(const appbox::TreeFolder& node)
{
    if (!node.virtual_dir.empty())
    {
        return node.virtual_dir;
    }

    if (node.import_name.empty())
    {
        return {};
    }

    return appbox::JoinViewPath(node.import_name, node.relative_dir);
}

} // namespace

namespace appbox
{

std::vector<TreeFolder> ChildFoldersOf(const PackModel& model, const TreeFolder& node)
{
    std::vector<TreeFolder> children;

    /*
     * The container holds the preset directories and a preset directory holds
     * the fixed entries of the tree, so only the folders of the view below them
     * are listed here: an imported folder, a folder below an import and a
     * folder the user created.
     */
    if (node.preset_id.empty() || (node.import_name.empty() && node.virtual_dir.empty()))
    {
        return children;
    }

    /*
     * The host folder of the node is the one the tooltip of the tree shows as
     * well, so the mapping of the model is used instead of composing the path
     * here. A folder the user created maps to no host folder, so only an
     * imported folder and a folder below one are listed from the host.
     */
    if (!node.import_name.empty())
    {
        std::wstring host_path;
        if (model.HostFolderPath(node.preset_id, node.import_name, node.relative_dir, host_path) && !host_path.empty())
        {
            const auto folder = std::filesystem::path(host_path);

            std::vector<std::wstring> subfolders;
            std::error_code           ec;
            for (auto it = std::filesystem::directory_iterator(folder, ec); it != std::filesystem::directory_iterator();
                 it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                if (it->is_directory(ec) && !ec)
                {
                    subfolders.push_back(it->path().filename().wstring());
                }
            }

            std::sort(subfolders.begin(), subfolders.end());
            for (const auto& name : subfolders)
            {
                TreeFolder child;
                child.preset_id = node.preset_id;
                child.import_name = node.import_name;
                child.relative_dir = node.relative_dir.empty() ? name : node.relative_dir + L"\\" + name;
                child.display_name = name;
                children.push_back(std::move(child));
            }
        }
    }

    /* The folders the user created below the directory come after the host content. */
    for (const auto& directory : model.VirtualDirectoriesOf(node.preset_id, NodeDirectoryPath(node)))
    {
        TreeFolder child;
        child.preset_id = node.preset_id;
        child.virtual_dir = directory.target_dir;
        child.display_name = LeafName(directory.target_dir);
        children.push_back(std::move(child));
    }

    return children;
}

std::vector<SubtreeFolder> SubtreeFoldersOf(const PackModel& model, const TreeFolder& node, std::size_t max_depth)
{
    std::vector<SubtreeFolder> entries;

    /*
     * The walk is iterative and holds the folders it still has to visit, so a
     * deep host folder cannot exhaust the stack of the caller. The children of
     * a folder are pushed in reverse, which makes the stack hand them out in
     * the order ChildFoldersOf() reports.
     */
    struct Pending
    {
        TreeFolder  folder;
        std::size_t parent;
        std::size_t depth;
    };

    std::vector<Pending> pending;

    const auto push_children = [&model, &pending](const TreeFolder& parent_node, std::size_t parent_index,
                                                  std::size_t depth) {
        auto children = ChildFoldersOf(model, parent_node);
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            pending.push_back(Pending{ *it, parent_index, depth });
        }
    };

    if (max_depth == 0)
    {
        return entries;
    }
    push_children(node, kTreeRootParent, 1);

    while (!pending.empty())
    {
        const Pending current = pending.back();
        pending.pop_back();

        const std::size_t index = entries.size();
        entries.push_back(SubtreeFolder{ current.folder, current.parent });

        if (current.depth < max_depth)
        {
            push_children(current.folder, index, current.depth + 1);
        }
    }

    return entries;
}

} // namespace appbox
