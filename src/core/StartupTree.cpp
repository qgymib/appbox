#include "StartupTree.hpp"
#include "PresetDirectory.hpp"
#include "StartupFileType.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <system_error>

namespace
{

/**
 * @brief Case insensitive comparison of two wide strings.
 * @param[in] a Left operand.
 * @param[in] b Right operand.
 * @return Negative, zero or positive value like wcscmp does.
 */
int CompareIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    const auto count = (std::min)(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto left = std::towlower(a[i]);
        const auto right = std::towlower(b[i]);
        if (left != right)
        {
            return left < right ? -1 : 1;
        }
    }

    if (a.size() == b.size())
    {
        return 0;
    }
    return a.size() < b.size() ? -1 : 1;
}

/**
 * @brief Case insensitive equality of two wide strings.
 * @param[in] a Left operand.
 * @param[in] b Right operand.
 * @return true when both strings are equal ignoring case.
 */
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    return CompareIgnoreCase(a, b) == 0;
}

/**
 * @brief Get the name of an entry from a path.
 * @param[in] path Path of the entry.
 * @return The last component of the path.
 */
std::wstring LeafName(const std::wstring& path)
{
    return path.substr(path.find_last_of(L'\\') + 1);
}

/**
 * @brief Order the rows of one folder.
 *
 * Folders come first so the tree keeps the shape of the sandbox view, then the
 * rows are ordered by their name ignoring the case. A case sensitive
 * comparison breaks the ties, which keeps the order of two names differing in
 * case only stable.
 *
 * @param[in] left Left row.
 * @param[in] right Right row.
 * @return true when the left row comes first.
 */
bool RowComesFirst(const std::unique_ptr<appbox::StartupNode>& left, const std::unique_ptr<appbox::StartupNode>& right)
{
    const bool left_folder = left->kind != appbox::StartupNodeKind::Executable;
    const bool right_folder = right->kind != appbox::StartupNodeKind::Executable;
    if (left_folder != right_folder)
    {
        return left_folder;
    }

    const int order = CompareIgnoreCase(left->label, right->label);
    if (order != 0)
    {
        return order < 0;
    }
    return left->label < right->label;
}

} // namespace

namespace appbox
{

StartupTree::StartupTree(const PackModel& model)
{
    model_ = &model;

    auto container = std::make_unique<StartupNode>();
    container->kind = StartupNodeKind::Container;
    container->label = kFilesystemContainerLabel;
    container_ = container.get();
    roots_.push_back(std::move(container));

    /*
     * The container holds the top level preset directories, a preset directory
     * holds the nested presets and its imports, exactly like the tree of the
     * filesystem workspace.
     */
    for (const auto& preset : ChildPresets(""))
    {
        AppendPreset(*container_, preset);
    }

    PruneEmpty(*container_);

    /*
     * A startup file keeps its place in the tree although its file is not on
     * this machine: a project which is imported on a machine where the packaged
     * application is not installed still lists its startup files, so the user
     * can see them and drop them.
     */
    files_ = model.StartupFiles();
    for (const auto& file : files_)
    {
        EnsureChoice(file);
    }

    SortChildren(*container_);

    /* The tree holds the model only while it is built, see the member. */
    model_ = nullptr;
}

const std::vector<std::unique_ptr<StartupNode>>& StartupTree::Roots() const
{
    return roots_;
}

std::vector<const StartupNode*> StartupTree::ExpandableRows() const
{
    std::vector<const StartupNode*> rows;
    for (const auto& root : roots_)
    {
        CollectExpandable(*root, rows);
    }
    return rows;
}

void StartupTree::AppendPreset(StartupNode& parent, const PresetDirectory& preset)
{
    auto node = std::make_unique<StartupNode>();
    node->kind = StartupNodeKind::Preset;
    node->label = preset.display_name;
    node->host_path = preset.real_path;
    node->preset_id = preset.id;
    node->parent = &parent;

    StartupNode* const preset_node = node.get();

    /* The nested presets are fixed entries, so they come before the imports. */
    for (const auto& nested : ChildPresets(preset.id))
    {
        AppendPreset(*preset_node, nested);
    }

    /*
     * The children of a preset are its imported folders: the host content of
     * the preset folder itself is never listed, because a preset directory is
     * the layer root of the sandbox view and not a folder inside it.
     */
    for (const auto& imported : model_->ImportsOf(preset.id))
    {
        AppendImport(*preset_node, imported);
    }

    /* The folders the user created come after the content which was imported. */
    for (const auto& directory : model_->VirtualDirectoriesOf(preset.id, L""))
    {
        AppendVirtualFolder(*preset_node, directory.target_dir);
    }

    parent.children.push_back(std::move(node));
}

void StartupTree::AppendImport(StartupNode& preset_node, const ImportedFolder& imported)
{
    auto node = std::make_unique<StartupNode>();
    node->kind = StartupNodeKind::Import;
    node->label = imported.import_name;
    node->host_path = imported.source_path;
    node->preset_id = imported.preset_id;
    node->import_name = imported.import_name;
    node->parent = &preset_node;

    AppendFolder(*node);

    preset_node.children.push_back(std::move(node));
}

void StartupTree::AppendFolder(StartupNode& node)
{
    /*
     * A folder the user created has no host counterpart, so only the folders of
     * the view below an imported folder are read from the host.
     */
    if (node.kind != StartupNodeKind::VirtualDirectory)
    {
        std::wstring host_path;
        if (model_->HostFolderPath(node.preset_id, node.import_name, node.relative_path, host_path) &&
            !host_path.empty())
        {
            std::error_code                           ec;
            const std::filesystem::directory_iterator end;
            for (auto it = std::filesystem::directory_iterator(host_path, ec); !ec && it != end; it.increment(ec))
            {
                const auto&     path = it->path();
                std::error_code entry_ec;
                const bool      is_directory = it->is_directory(entry_ec);

                if (!is_directory && !it->is_regular_file(entry_ec))
                {
                    /* A link or a device of the host folder is not a file of the view. */
                    continue;
                }

                if (!is_directory && !IsStartableFile(path.wstring()))
                {
                    continue;
                }

                auto child = std::make_unique<StartupNode>();
                child->kind = is_directory ? StartupNodeKind::Directory : StartupNodeKind::Executable;
                child->label = path.filename().wstring();
                child->host_path = path.wstring();
                child->preset_id = node.preset_id;
                child->import_name = node.import_name;
                child->relative_path =
                    node.relative_path.empty() ? child->label : node.relative_path + L"\\" + child->label;
                child->parent = &node;

                if (is_directory)
                {
                    AppendFolder(*child);
                }

                node.children.push_back(std::move(child));
            }
        }
    }

    /* The files which were imported on their own follow the host content. */
    AppendImportedFiles(node);

    /* The folders the user created inside the folder come after its content. */
    for (const auto& directory : model_->VirtualDirectoriesOf(node.preset_id, TargetDirOf(node)))
    {
        AppendVirtualFolder(node, directory.target_dir);
    }
}

void StartupTree::AppendVirtualFolder(StartupNode& parent, const std::wstring& target_dir)
{
    auto node = std::make_unique<StartupNode>();
    node->kind = StartupNodeKind::VirtualDirectory;
    node->label = LeafName(target_dir);
    node->preset_id = parent.preset_id;
    node->virtual_dir = target_dir;
    node->parent = &parent;

    AppendFolder(*node);

    parent.children.push_back(std::move(node));
}

void StartupTree::AppendImportedFiles(StartupNode& node)
{
    const auto target_dir = TargetDirOf(node);
    for (const auto& file : model_->FilesOf(node.preset_id, target_dir))
    {
        if (!IsStartableFile(file.file_name))
        {
            continue;
        }

        /*
         * An imported file extends an imported folder, so the first segment of
         * the target directory names the import root the file is stored
         * relative to.
         */
        auto child = std::make_unique<StartupNode>();
        child->kind = StartupNodeKind::Executable;
        child->label = file.file_name;
        child->host_path = file.source_path;
        child->preset_id = node.preset_id;
        child->import_name = Split(file.target_dir, L"\\").front();
        child->relative_path = ImportRelativePath(file.target_dir, file.file_name);
        child->parent = &node;

        node.children.push_back(std::move(child));
    }
}

StartupNode* StartupTree::EnsurePreset(const std::string& preset_id)
{
    if (container_ == nullptr)
    {
        return nullptr;
    }

    return EnsurePresetBelow(*container_, preset_id);
}

StartupNode* StartupTree::EnsurePresetBelow(StartupNode& parent, const std::string& preset_id)
{
    PresetDirectory preset;
    if (!FindPresetDirectory(preset_id, preset))
    {
        return nullptr;
    }

    /* A nested preset hangs below the preset which holds it. */
    StartupNode* owner = &parent;
    if (!preset.parent_id.empty())
    {
        owner = EnsurePresetBelow(parent, preset.parent_id);
        if (owner == nullptr)
        {
            return nullptr;
        }
    }

    for (const auto& child : owner->children)
    {
        if (child->kind == StartupNodeKind::Preset && child->preset_id == preset_id)
        {
            return child.get();
        }
    }

    auto node = std::make_unique<StartupNode>();
    node->kind = StartupNodeKind::Preset;
    node->label = preset.display_name;
    node->host_path = preset.real_path;
    node->preset_id = preset.id;
    node->parent = owner;

    StartupNode* const created = node.get();
    owner->children.push_back(std::move(node));
    return created;
}

StartupNode* StartupTree::FindPresetBelow(StartupNode& parent, const std::string& preset_id)
{
    for (const auto& child : parent.children)
    {
        if (child->kind != StartupNodeKind::Preset)
        {
            continue;
        }

        if (child->preset_id == preset_id)
        {
            return child.get();
        }

        StartupNode* const nested = FindPresetBelow(*child, preset_id);
        if (nested != nullptr)
        {
            return nested;
        }
    }

    return nullptr;
}

StartupNode* StartupTree::EnsureChoice(const StartupFile& file)
{
    StartupNode* const preset = EnsurePreset(file.preset_id);
    if (preset == nullptr)
    {
        return nullptr;
    }

    ImportedFolder imported;
    if (!model_->GetImport(file.preset_id, file.import_name, imported))
    {
        /* The import is gone, so the file is no folder of the view. */
        return nullptr;
    }

    StartupNode* import = nullptr;
    for (const auto& child : preset->children)
    {
        if (child->kind == StartupNodeKind::Import && EqualsIgnoreCase(child->label, file.import_name))
        {
            import = child.get();
            break;
        }
    }

    if (import == nullptr)
    {
        auto node = std::make_unique<StartupNode>();
        node->kind = StartupNodeKind::Import;
        node->label = imported.import_name;
        node->host_path = imported.source_path;
        node->preset_id = imported.preset_id;
        node->import_name = imported.import_name;
        node->parent = preset;

        import = node.get();
        preset->children.push_back(std::move(node));
    }

    /*
     * The folders of the way to the file. A missing folder can only be a folder
     * of the host: a file inside a folder the user created is always imported,
     * so its row was built from the model already.
     */
    const auto   segments = Split(file.relative_path, L"\\");
    StartupNode* current = import;
    for (std::size_t index = 0; index + 1 < segments.size(); ++index)
    {
        const auto& segment = segments[index];
        if (segment.empty())
        {
            continue;
        }

        StartupNode* next = nullptr;
        for (const auto& child : current->children)
        {
            if (child->kind != StartupNodeKind::Executable && EqualsIgnoreCase(child->label, segment))
            {
                next = child.get();
                break;
            }
        }

        if (next == nullptr)
        {
            auto node = std::make_unique<StartupNode>();
            node->kind = StartupNodeKind::Directory;
            node->label = segment;
            node->host_path = current->host_path.empty()
                                  ? std::wstring()
                                  : (std::filesystem::path(current->host_path) / segment).wstring();
            node->preset_id = current->preset_id;
            node->import_name = current->import_name;
            node->relative_path = current->relative_path.empty() ? segment : current->relative_path + L"\\" + segment;
            node->parent = current;

            next = node.get();
            current->children.push_back(std::move(node));
        }

        current = next;
    }

    const std::wstring file_name = segments.empty() ? std::wstring() : segments.back();
    if (file_name.empty())
    {
        return nullptr;
    }

    for (const auto& child : current->children)
    {
        if (child->kind == StartupNodeKind::Executable && EqualsIgnoreCase(child->label, file_name))
        {
            return child.get();
        }
    }

    auto node = std::make_unique<StartupNode>();
    node->kind = StartupNodeKind::Executable;
    node->label = file_name;
    node->host_path = (std::filesystem::path(imported.source_path) / file.relative_path).wstring();
    node->preset_id = current->preset_id;
    node->import_name = current->import_name;
    node->relative_path = file.relative_path;
    node->parent = current;

    StartupNode* const created = node.get();
    current->children.push_back(std::move(node));
    return created;
}

void StartupTree::PruneEmpty(StartupNode& node)
{
    for (const auto& child : node.children)
    {
        PruneEmpty(*child);
    }

    /*
     * A row which holds no startable file below it is not offered, which is
     * what hides a preset directory, an imported folder and a folder of the
     * host which no startup file can come from. A row which names a startable
     * file is always kept.
     */
    node.children.erase(std::remove_if(node.children.begin(), node.children.end(),
                                       [](const std::unique_ptr<StartupNode>& child) {
                                           return child->kind != StartupNodeKind::Executable && child->children.empty();
                                       }),
                        node.children.end());
}

void StartupTree::CollectExpandable(const StartupNode& node, std::vector<const StartupNode*>& rows)
{
    /*
     * A row without a child cannot be expanded, which is the state of a row
     * which names a startable file and of the container row of a tree without
     * an import.
     */
    if (node.children.empty())
    {
        return;
    }

    rows.push_back(&node);
    for (const auto& child : node.children)
    {
        CollectExpandable(*child, rows);
    }
}

void StartupTree::SortChildren(StartupNode& node)
{
    std::sort(node.children.begin(), node.children.end(), RowComesFirst);
    for (const auto& child : node.children)
    {
        SortChildren(*child);
    }
}

std::wstring StartupTree::TargetDirOf(const StartupNode& node)
{
    if (node.kind == StartupNodeKind::VirtualDirectory)
    {
        return node.virtual_dir;
    }

    return node.relative_path.empty() ? node.import_name : node.import_name + L"\\" + node.relative_path;
}

std::wstring StartupTree::ImportRelativePath(const std::wstring& target_dir, const std::wstring& file_name)
{
    /*
     * The first segment of the target directory is the name of the imported
     * folder, which is the root a startup file is stored relative to.
     */
    const auto   segments = Split(target_dir, L"\\");
    std::wstring relative;
    for (std::size_t index = 1; index < segments.size(); ++index)
    {
        if (segments[index].empty())
        {
            continue;
        }
        if (!relative.empty())
        {
            relative.push_back(L'\\');
        }
        relative += segments[index];
    }

    if (!relative.empty())
    {
        relative.push_back(L'\\');
    }
    relative += file_name;
    return relative;
}

StartupNode* StartupTree::FindNode(StartupNode& import_root, const std::wstring& relative_path) const
{
    StartupNode* current = &import_root;
    for (const auto& segment : Split(relative_path, L"\\"))
    {
        if (segment.empty())
        {
            continue;
        }

        StartupNode* next = nullptr;
        for (const auto& child : current->children)
        {
            if (EqualsIgnoreCase(child->label, segment))
            {
                next = child.get();
                break;
            }
        }

        if (next == nullptr)
        {
            return nullptr;
        }
        current = next;
    }

    return current;
}

StartupNode* StartupTree::FindChoice(const StartupFile& file) const
{
    for (const auto& root : roots_)
    {
        StartupNode* const preset = FindPresetBelow(*root, file.preset_id);
        if (preset == nullptr)
        {
            continue;
        }

        for (const auto& child : preset->children)
        {
            if (child->kind == StartupNodeKind::Import && EqualsIgnoreCase(child->label, file.import_name))
            {
                return FindNode(*child, file.relative_path);
            }
        }
    }

    return nullptr;
}

bool StartupTree::IsCheckable(const StartupNode& node)
{
    return node.kind == StartupNodeKind::Executable;
}

bool StartupTree::Matches(const StartupNode& node, const StartupFile& file)
{
    return node.preset_id == file.preset_id && EqualsIgnoreCase(node.import_name, file.import_name) &&
           EqualsIgnoreCase(node.relative_path, file.relative_path);
}

StartupFile StartupTree::MakeStartupFile(const StartupNode& node)
{
    StartupFile file;
    file.preset_id = node.preset_id;
    file.import_name = node.import_name;
    file.relative_path = node.relative_path;
    return file;
}

const std::vector<StartupFile>& StartupTree::Files() const
{
    return files_;
}

bool StartupTree::HasFiles() const
{
    return !files_.empty();
}

int StartupTree::IndexOf(const StartupNode& node) const
{
    /*
     * Only a row which names a startable file can be a startup file: a folder
     * of the view may carry the very path a file of the list carries.
     */
    if (!IsCheckable(node))
    {
        return -1;
    }

    for (std::size_t i = 0; i < files_.size(); ++i)
    {
        if (Matches(node, files_[i]))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool StartupTree::Contains(const StartupNode& node) const
{
    return IndexOf(node) >= 0;
}

bool StartupTree::IsAutoStart(const StartupNode& node) const
{
    const auto index = IndexOf(node);
    return index >= 0 && files_[static_cast<std::size_t>(index)].auto_start;
}

std::wstring StartupTree::TriggerOf(const StartupNode& node) const
{
    const auto index = IndexOf(node);
    return index < 0 ? std::wstring() : files_[static_cast<std::size_t>(index)].trigger;
}

bool StartupTree::SetAutoStart(const StartupNode& node, bool auto_start)
{
    if (!IsCheckable(node))
    {
        return false;
    }

    const auto index = IndexOf(node);
    if (index >= 0)
    {
        files_[static_cast<std::size_t>(index)].auto_start = auto_start;
        return true;
    }

    if (!auto_start)
    {
        /* A row which is not a startup file has no flag to clear. */
        return false;
    }

    auto file = MakeStartupFile(node);
    file.trigger = FreeStartupTrigger(files_, DefaultStartupTrigger(file.relative_path));
    file.auto_start = true;
    files_.push_back(std::move(file));
    return true;
}

bool StartupTree::SetTrigger(const StartupNode& node, const std::wstring& trigger, std::string& error)
{
    const auto index = IndexOf(node);
    if (index < 0)
    {
        /* Only a startup file carries a trigger. */
        return false;
    }

    const auto trimmed = TrimStartupTrigger(trigger);
    if (trimmed.empty())
    {
        error = "the startup file trigger is empty";
        return false;
    }

    for (std::size_t i = 0; i < files_.size(); ++i)
    {
        if (static_cast<int>(i) != index && EqualsIgnoreCase(files_[i].trigger, trimmed))
        {
            error = "the startup file trigger '" + WideToUTF8(trimmed) + "' is used twice";
            return false;
        }
    }

    files_[static_cast<std::size_t>(index)].trigger = trimmed;
    return true;
}

bool StartupTree::Remove(const StartupNode& node)
{
    const auto index = IndexOf(node);
    if (index < 0)
    {
        return false;
    }

    files_.erase(files_.begin() + index);
    return true;
}

} // namespace appbox
