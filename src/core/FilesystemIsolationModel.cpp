#include "FilesystemIsolationModel.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cwctype>
#include <utility>

namespace
{

/**
 * @brief Compare two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts are equal ignoring the case.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(left[index]) != std::towlower(right[index]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Order two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when left has to be placed before right.
 */
bool LessIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    const std::size_t shared = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < shared; ++index)
    {
        const wchar_t left_char = std::towlower(left[index]);
        const wchar_t right_char = std::towlower(right[index]);
        if (left_char != right_char)
        {
            return left_char < right_char;
        }
    }
    return left.size() < right.size();
}

/**
 * @brief Quote a wide text for an English error description.
 * @param[in] text The text to quote.
 * @return The quoted UTF-8 text.
 */
std::string Quote(const std::wstring& text)
{
    return "'" + appbox::WideToUTF8(text) + "'";
}

/**
 * @brief Describe the reason why a mode cannot be used for an entry kind.
 * @param[in] isolation The refused isolation mode.
 * @return The error description.
 */
std::string RefusedMode(const appbox::FilesystemIsolation isolation)
{
    return std::string("the isolation mode '")
           + appbox::filesystem_isolation::IsolationToken(isolation)
           + "' cannot be used for a file";
}

} // namespace

namespace appbox
{

const std::vector<std::wstring>& FilesystemIsolationNames()
{
    static const std::vector<std::wstring> names = { L"Full", L"Write Copy", L"Whiteout" };
    return names;
}

std::wstring FilesystemIsolationName(FilesystemIsolation isolation)
{
    const auto& names = FilesystemIsolationNames();
    const auto index = static_cast<std::size_t>(isolation);
    if (index >= names.size())
    {
        return names.front();
    }
    return names[index];
}

const std::vector<std::wstring>& FilesystemIsolationNamesFor(FilesystemEntryKind kind)
{
    static const std::vector<std::wstring> folder_names = FilesystemIsolationNames();
    static const std::vector<std::wstring> file_names = { L"Full", L"Whiteout" };

    return kind == FilesystemEntryKind::Directory ? folder_names : file_names;
}

bool ParseFilesystemIsolationName(const std::wstring& name, FilesystemIsolation& out)
{
    const auto& names = FilesystemIsolationNames();
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (EqualsIgnoreCase(names[index], name))
        {
            out = static_cast<FilesystemIsolation>(index);
            return true;
        }
    }
    return false;
}

FilesystemIsolation DefaultFilesystemIsolation(FilesystemEntryKind kind)
{
    return kind == FilesystemEntryKind::Directory ? FilesystemIsolation::WriteCopy
                                                  : FilesystemIsolation::Full;
}

FilesystemIsolation FilesystemIsolationForKind(FilesystemIsolation isolation, FilesystemEntryKind kind)
{
    if (kind == FilesystemEntryKind::File && isolation == FilesystemIsolation::WriteCopy)
    {
        /*
         * A single file cannot be merged with the host folder by folder: both
         * `Full` and `WriteCopy` keep the host content visible and send every
         * write of the file into the sandbox.
         */
        return FilesystemIsolation::Full;
    }
    return isolation;
}

std::vector<std::wstring> SplitViewPath(const std::wstring& path)
{
    std::vector<std::wstring> parts;
    std::wstring current;

    for (const wchar_t character : path)
    {
        if (character == kFilesystemViewPathSeparator || character == L'/')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }

    if (!current.empty())
    {
        parts.push_back(current);
    }
    return parts;
}

std::wstring JoinViewPath(const std::wstring& parent, const std::wstring& name)
{
    if (parent.empty())
    {
        return name;
    }
    if (name.empty())
    {
        return parent;
    }
    return parent + kFilesystemViewPathSeparator + name;
}

std::wstring ViewPathParent(const std::wstring& path)
{
    const auto normalized = NormalizeViewPath(path);
    const auto position = normalized.find_last_of(kFilesystemViewPathSeparator);
    if (position == std::wstring::npos)
    {
        return {};
    }
    return normalized.substr(0, position);
}

std::wstring ViewPathLeafName(const std::wstring& path)
{
    const auto normalized = NormalizeViewPath(path);
    const auto position = normalized.find_last_of(kFilesystemViewPathSeparator);
    if (position == std::wstring::npos)
    {
        return normalized;
    }
    return normalized.substr(position + 1);
}

std::wstring NormalizeViewPath(const std::wstring& path)
{
    std::wstring normalized;
    for (const auto& part : SplitViewPath(path))
    {
        if (part == L".")
        {
            continue;
        }
        if (part == L"..")
        {
            /* A parent reference would leave the virtual filesystem. */
            return {};
        }
        normalized = JoinViewPath(normalized, part);
    }
    return normalized;
}

bool ViewPathEquals(const std::wstring& left, const std::wstring& right)
{
    return EqualsIgnoreCase(NormalizeViewPath(left), NormalizeViewPath(right));
}

bool IsViewPathBelow(const std::wstring& path, const std::wstring& ancestor)
{
    const auto normalized = NormalizeViewPath(path);
    const auto normalized_ancestor = NormalizeViewPath(ancestor);
    if (normalized_ancestor.empty() || normalized.size() <= normalized_ancestor.size())
    {
        return false;
    }

    const auto prefix = normalized.substr(0, normalized_ancestor.size());
    if (!EqualsIgnoreCase(prefix, normalized_ancestor))
    {
        return false;
    }

    /* The ancestor has to be a whole component prefix. */
    return normalized[normalized_ancestor.size()] == kFilesystemViewPathSeparator;
}

void FilesystemIsolationModel::Reset()
{
    entries_.clear();
}

bool FilesystemIsolationModel::IsEmpty() const
{
    return entries_.empty();
}

bool FilesystemIsolationModel::SetIsolation(const std::wstring& view_path, FilesystemEntryKind kind,
                                            FilesystemIsolation isolation, std::string& error)
{
    const auto path = NormalizeViewPath(view_path);
    if (path.empty())
    {
        error = "the path does not name an entry of the virtual filesystem";
        return false;
    }
    if (!filesystem_isolation::IsAllowed(isolation, kind))
    {
        error = RefusedMode(isolation);
        return false;
    }

    const auto index = EntryIndex(path);
    if (index >= 0)
    {
        auto& entry = entries_[static_cast<std::size_t>(index)];
        entry.kind = kind;
        entry.isolation = isolation;
        return true;
    }

    FilesystemIsolationEntry entry;
    entry.path = path;
    entry.kind = kind;
    entry.isolation = isolation;
    entries_.push_back(std::move(entry));
    SortEntries();
    return true;
}

bool FilesystemIsolationModel::AddEntry(const FilesystemIsolationEntry& entry, std::string& error)
{
    const auto path = NormalizeViewPath(entry.path);
    if (path.empty())
    {
        error = "the path does not name an entry of the virtual filesystem";
        return false;
    }
    if (!filesystem_isolation::IsAllowed(entry.isolation, entry.kind))
    {
        error = RefusedMode(entry.isolation);
        return false;
    }
    if (EntryIndex(path) >= 0)
    {
        error = "the entry " + Quote(path) + " is listed twice";
        return false;
    }

    FilesystemIsolationEntry stored;
    stored.path = path;
    stored.kind = entry.kind;
    stored.isolation = entry.isolation;
    entries_.push_back(std::move(stored));
    SortEntries();
    return true;
}

bool FilesystemIsolationModel::RemoveSubtree(const std::wstring& view_path)
{
    const auto path = NormalizeViewPath(view_path);
    if (path.empty())
    {
        return false;
    }

    const auto size = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&path](const FilesystemIsolationEntry& entry) {
                                      return ViewPathEquals(entry.path, path)
                                             || IsViewPathBelow(entry.path, path);
                                  }),
                   entries_.end());
    return entries_.size() != size;
}

bool FilesystemIsolationModel::HasExplicitIsolation(const std::wstring& view_path) const
{
    const auto path = NormalizeViewPath(view_path);
    return !path.empty() && EntryIndex(path) >= 0;
}

FilesystemIsolation FilesystemIsolationModel::EffectiveIsolation(const std::wstring& view_path,
                                                                FilesystemEntryKind kind) const
{
    auto current = NormalizeViewPath(view_path);
    while (!current.empty())
    {
        const auto index = EntryIndex(current);
        if (index >= 0)
        {
            return FilesystemIsolationForKind(entries_[static_cast<std::size_t>(index)].isolation, kind);
        }
        current = ViewPathParent(current);
    }
    return DefaultFilesystemIsolation(kind);
}

const std::vector<FilesystemIsolationEntry>& FilesystemIsolationModel::Entries() const
{
    return entries_;
}

std::ptrdiff_t FilesystemIsolationModel::EntryIndex(const std::wstring& normalized_path) const
{
    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        if (EqualsIgnoreCase(entries_[index].path, normalized_path))
        {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

void FilesystemIsolationModel::SortEntries()
{
    std::sort(entries_.begin(), entries_.end(),
              [](const FilesystemIsolationEntry& left, const FilesystemIsolationEntry& right) {
                  return LessIgnoreCase(left.path, right.path);
              });
}

} // namespace appbox
