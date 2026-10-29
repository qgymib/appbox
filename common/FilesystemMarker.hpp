#ifndef APPBOX_COMMON_FILESYSTEM_MARKER_HPP
#define APPBOX_COMMON_FILESYSTEM_MARKER_HPP

#include <string>
#include <string_view>

namespace appbox::filesystem_marker
{

/**
 * @brief Suffix of the whiteout marker of an entry of the view.
 *
 * The marker is an empty file which sits next to the entry it hides and is
 * named `<name>.$APPBOX_DELETE$`; the sandbox honours a marker which any layer
 * of the view holds, which is what lets an archive carry the delete of an
 * entry. The packer writes such a marker for every entry a captured snapshot
 * reports as removed, see `src/core/PackModel.hpp`.
 *
 * The sandbox spells the same name with its own macro
 * (`APPBOX_SANDBOX_WHITEOUT_SUFFIX_W` of `sandbox/utils/Defines.hpp`), because
 * the packer must not include a header of the sandbox; `test/unit/FilesystemMarker.cpp`
 * pins the two spellings against each other.
 */
inline constexpr std::wstring_view kWhiteoutSuffix = L".$APPBOX_DELETE$";

/**
 * @brief Name of the opaque marker of a folder of the view.
 *
 * The marker makes the resolver stop looking into the lower layers for the
 * folder it sits in. The packer does not write one today; the name is part of
 * this header so the whole vocabulary of the view lives in one place.
 */
inline constexpr std::wstring_view kOpaqueName = L".$APPBOX_OPAQUE$";

/**
 * @brief Build the name of the whiteout marker which hides an entry.
 *
 * @param[in] name Name of the entry to hide.
 * @return The name of the marker, e.g. `L"tool.exe.$APPBOX_DELETE$"`.
 */
inline std::wstring WhiteoutNameOf(std::wstring_view name)
{
    return std::wstring(name) + std::wstring(kWhiteoutSuffix);
}

/**
 * @brief Path of the whiteout marker which hides an entry of the view.
 *
 * The marker is a sibling of the entry, so the caller has to place it inside
 * the layer which hides the entry. The marker of a stream is a stream of the
 * file which carries it, because a file name cannot carry a colon.
 *
 * The helper is the one implementation of the name, which the resolver of
 * `common/ResolveCore.hpp` and the sandbox use alike: the sandbox spells the
 * suffix with its own macro, see `sandbox/filesystem/MarkerName.hpp`, and the
 * two spellings are pinned against each other by `test/unit/FilesystemMarker.cpp`.
 *
 * @param[in] path Path of the entry inside a layer.
 * @return Path of the marker of the entry.
 */
inline std::wstring WhiteoutPathOf(std::wstring_view path)
{
    return std::wstring(path) + std::wstring(kWhiteoutSuffix);
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
inline std::wstring OpaquePathOf(std::wstring_view directory)
{
    return std::wstring(directory) + L"\\" + std::wstring(kOpaqueName);
}

} // namespace appbox::filesystem_marker

#endif // APPBOX_COMMON_FILESYSTEM_MARKER_HPP
