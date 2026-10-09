#ifndef APPBOX_SANDBOX_FILESYSTEM_MARKER_NAME_HPP
#define APPBOX_SANDBOX_FILESYSTEM_MARKER_NAME_HPP

#include <cstddef>
#include <cwctype>
#include <string>
#include "filesystem/StreamName.hpp"
#include "utils/Defines.hpp"

namespace appbox::filesystem
{

/**
 * @brief Whether two names are equal, ignoring the case.
 *
 * The volumes of the layers compare names without regard to the case, so the
 * names of the view are compared the same way: a layer which holds
 * `x.$appbox_delete$` holds the whiteout marker of `x` as well.
 *
 * @param[in] left First name to compare.
 * @param[in] right Second name to compare.
 * @return true when the names differ in the case of their characters alone.
 */
inline bool MarkerNamesEqual(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towupper(left[index]) != std::towupper(right[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a name ends with a suffix, ignoring the case.
 * @param[in] name Name to inspect.
 * @param[in] suffix Suffix to look for.
 * @return true when the name ends with the suffix.
 */
inline bool MarkerNameEndsWith(const std::wstring& name, const std::wstring& suffix)
{
    if (suffix.size() > name.size())
    {
        return false;
    }

    const std::size_t offset = name.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index)
    {
        if (std::towupper(name[offset + index]) != std::towupper(suffix[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a name is one of the reserved names of the view.
 *
 * A whiteout marker is named after the entry it hides
 * (`<name>.$APPBOX_DELETE$`) and the marker which makes a folder opaque carries
 * a name of its own (`.$APPBOX_OPAQUE$`), so both shapes name the view rather
 * than an entry it holds: no entry of the view carries such a name, see
 * `ReservedMarkerNamePlacement()`.
 *
 * @param[in] name Name of an entry to inspect.
 * @return true when the name is a name of the view itself.
 */
inline bool IsReservedMarkerName(const std::wstring& name)
{
    return MarkerNamesEqual(name, APPBOX_SANDBOX_OPAQUE_NAME_W) ||
           MarkerNameEndsWith(name, APPBOX_SANDBOX_WHITEOUT_SUFFIX_W);
}

/**
 * @brief Whether a component of a path of the view carries a reserved name.
 *
 * A component which names an alternate data stream carries two names: the file
 * which holds the stream and the stream itself. The marker of a stream is a
 * stream of the file it belongs to (`file.txt:stream.$APPBOX_DELETE$`), so the
 * name of the stream is reserved as well, and the streams of a file whose own
 * name is reserved are not part of the view either.
 *
 * @param[in] component Component of a path, without a separator.
 * @return true when the component names a marker of the view.
 */
inline bool IsReservedMarkerComponent(const std::wstring& component)
{
    if (IsReservedMarkerName(component))
    {
        return true;
    }

    const std::size_t separator = StreamNameSeparator(component);
    if (separator == std::wstring::npos)
    {
        return false;
    }

    return IsReservedMarkerName(component.substr(0, separator)) ||
           IsReservedMarkerName(component.substr(separator + 1));
}

/**
 * @brief Where a path of the view meets the reserved names of the markers.
 */
enum class MarkerNamePlacement
{
    /** No component of the path carries a reserved name. */
    None,

    /** The entry the path names carries a reserved name. */
    Entry,

    /** A component above the entry carries a reserved name. */
    Parent,
};

/**
 * @brief Placement of the reserved names inside a path of the view.
 *
 * The names of the markers are names of the view and not of the entries it
 * holds, so a path which carries one is not part of the view: the entry of a
 * reserved name does not exist, and no entry hangs below a component which
 * carries one. The caller decides the failure from the placement, because a
 * call which creates an entry reports the name it refuses while a call which
 * looks an entry up reports an entry which is missing.
 *
 * The path is walked component by component below its drive, so the drive of
 * the path (`\??\C:`) and the prefix of the object namespace are never
 * inspected.
 *
 * @param[in] viewPath Path of the view.
 * @return Placement of the reserved names inside the path.
 */
inline MarkerNamePlacement ReservedMarkerNamePlacement(const std::wstring& viewPath)
{
    constexpr wchar_t kObjectNamespacePrefix[] = L"\\??\\";

    std::size_t begin = 0;
    if (viewPath.compare(0, 4, kObjectNamespacePrefix) == 0)
    {
        begin = 4;
    }

    /* The first component of the path is the drive, which names no entry. */
    const std::size_t drive_end = viewPath.find(L'\\', begin);
    if (drive_end == std::wstring::npos)
    {
        return MarkerNamePlacement::None;
    }

    bool        entry_reserved = false;
    std::size_t pos = drive_end + 1;
    for (;;)
    {
        const std::size_t separator = viewPath.find(L'\\', pos);
        const bool        is_last = (separator == std::wstring::npos);
        const auto        component = viewPath.substr(pos, is_last ? std::wstring::npos : separator - pos);

        if (!component.empty() && IsReservedMarkerComponent(component))
        {
            if (!is_last)
            {
                /* Nothing hangs below a name the view reserves. */
                return MarkerNamePlacement::Parent;
            }
            entry_reserved = true;
        }

        if (is_last)
        {
            break;
        }
        pos = separator + 1;
    }

    return entry_reserved ? MarkerNamePlacement::Entry : MarkerNamePlacement::None;
}

/**
 * @brief Path of the whiteout marker which hides an entry of the view.
 *
 * The marker is a sibling of the entry, so the caller has to place it inside
 * the layer which hides the entry, see `sandbox/hook/NtDeleteFile.cpp`. The
 * marker of a stream is a stream of the file which carries it, because a file
 * name cannot carry a colon.
 *
 * @param[in] path Path of the entry inside a layer.
 * @return Path of the marker of the entry.
 */
inline std::wstring WhiteoutPathOf(const std::wstring& path)
{
    return path + APPBOX_SANDBOX_WHITEOUT_SUFFIX_W;
}

/**
 * @brief Path of the opaque marker of a folder of the view.
 *
 * The marker is a child of the folder, so the caller has to place it inside the
 * layer which holds the folder.
 *
 * @param[in] directory Path of the folder inside a layer.
 * @return Path of the marker of the folder.
 */
inline std::wstring OpaquePathOf(const std::wstring& directory)
{
    return directory + L"\\" + APPBOX_SANDBOX_OPAQUE_NAME_W;
}

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_MARKER_NAME_HPP
