#ifndef APPBOX_LOADER_UTILS_HIVE_MERGE_HPP
#define APPBOX_LOADER_UTILS_HIVE_MERGE_HPP

#include <string>

namespace appbox
{

/**
 * @brief Merge the keys and the values of one hive into another.
 *
 * The content of the source hive is applied on top of the target hive: a key
 * of the source is created when the target does not hold it, and a key or a
 * value the target already holds is overridden by the entry of the same name
 * of the source. Every other entry of the target stays in place, so the
 * caller can apply the hives of several layers one after the other and let
 * the last one win.
 *
 * The whiteout store of the target is never touched, because it records the
 * deletions of the sandbox and not the content of a layer, and the store of
 * the source is skipped: a patch package carries the resources of a packed
 * application and no deletion marker.
 *
 * The merge is the loader side of the registry domain of a patch layer: the
 * hive of a package is merged into the hive the sandbox mounts, which keeps
 * the sandbox unaware of the layers.
 *
 * A missing target is created by the mount of the hive, exactly like the
 * sandbox creates it when it mounts a hive file which does not exist yet. A
 * missing source is an error: mounting it would create an empty hive inside
 * the cache entry of a package.
 *
 * @param[in] target_hive DOS path of the hive which receives the content.
 * @param[in] source_hive DOS path of the hive which is applied on top.
 * @param[out] error Error description on failure.
 * @return true when the whole content of the source was applied.
 */
bool MergeHiveInto(const std::wstring& target_hive, const std::wstring& source_hive, std::string& error);

} // namespace appbox

#endif // APPBOX_LOADER_UTILS_HIVE_MERGE_HPP
