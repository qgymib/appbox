#include "StartupTree.hpp"
#include "PresetDirectory.hpp"
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
 * @brief Check the file extension case insensitively.
 * @param[in] path Host path.
 * @param[in] extension Expected extension including the dot.
 * @return true when the extension matches.
 */
bool HasExtension(const std::wstring& path, const std::wstring& extension)
{
    return EqualsIgnoreCase(std::filesystem::path(path).extension().wstring(), extension);
}

/**
 * @brief Order the rows of one folder.
 *
 * Folders come first so the tree keeps the shape of the host directory, then
 * the rows are ordered by their name ignoring the case. A case sensitive
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
    for (const auto& preset : PresetDirectories())
    {
        auto preset_node = std::make_unique<StartupNode>();
        preset_node->kind = StartupNodeKind::Preset;
        preset_node->label = preset.display_name;
        preset_node->host_path = preset.real_path;
        preset_node->preset_id = preset.id;

        /*
         * The children of a preset are its imports, they come from the model
         * and must not be replaced by the host content of the preset folder.
         */
        preset_node->populated = true;

        for (const auto& imported : model.ImportsOf(preset.id))
        {
            auto import_node = std::make_unique<StartupNode>();
            import_node->kind = StartupNodeKind::Import;
            import_node->label = imported.import_name;
            import_node->host_path = imported.source_path;
            import_node->preset_id = imported.preset_id;
            import_node->import_name = imported.import_name;
            import_node->parent = preset_node.get();

            preset_node->children.push_back(std::move(import_node));
        }

        roots_.push_back(std::move(preset_node));
    }
}

const std::vector<std::unique_ptr<StartupNode>>& StartupTree::Roots() const
{
    return roots_;
}

void StartupTree::EnsureChildren(StartupNode& node)
{
    if (node.populated)
    {
        return;
    }

    EnumerateChildren(node);
}

void StartupTree::EnumerateChildren(StartupNode& node)
{
    node.populated = true;

    std::error_code                     ec;
    std::filesystem::directory_iterator it(node.host_path, ec);
    if (ec)
    {
        /* An unreadable folder simply stays empty. */
        return;
    }

    std::vector<std::unique_ptr<StartupNode>> children;
    const std::filesystem::directory_iterator end;
    while (it != end)
    {
        const auto&     path = it->path();
        std::error_code entry_ec;
        const bool      is_directory = it->is_directory(entry_ec);
        const bool      is_file = !is_directory && it->is_regular_file(entry_ec);

        if (is_directory || (is_file && HasExtension(path.wstring(), L".exe")))
        {
            auto child = std::make_unique<StartupNode>();
            child->kind = is_directory ? StartupNodeKind::Directory : StartupNodeKind::Executable;
            child->label = path.filename().wstring();
            child->host_path = path.wstring();
            child->preset_id = node.preset_id;
            child->import_name = node.import_name;
            child->relative_path =
                node.relative_path.empty() ? child->label : node.relative_path + L"\\" + child->label;
            child->parent = &node;
            children.push_back(std::move(child));
        }

        it.increment(ec);
        if (ec)
        {
            break;
        }
    }

    std::sort(children.begin(), children.end(), RowComesFirst);
    node.children = std::move(children);
}

StartupNode* StartupTree::FindNode(StartupNode& import_root, const std::wstring& relative_path)
{
    StartupNode* current = &import_root;
    for (const auto& segment : Split(relative_path, L"\\"))
    {
        if (segment.empty())
        {
            continue;
        }

        EnsureChildren(*current);

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

StartupNode* StartupTree::FindChoice(const StartupFile& file)
{
    for (const auto& root : roots_)
    {
        if (root->preset_id != file.preset_id)
        {
            continue;
        }

        for (const auto& import_node : root->children)
        {
            if (!EqualsIgnoreCase(import_node->import_name, file.import_name))
            {
                continue;
            }

            return FindNode(*import_node, file.relative_path);
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

void StartupTree::Preselect(const std::vector<StartupFile>& files)
{
    files_ = files;
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
    return IsCheckable(node) && IndexOf(node) >= 0;
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
