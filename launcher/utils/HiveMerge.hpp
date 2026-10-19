#ifndef APPBOX_LAUNCHER_UTILS_HIVE_MERGE_HPP
#define APPBOX_LAUNCHER_UTILS_HIVE_MERGE_HPP

#include "RegistryIsolation.hpp"
#include <string>
#include <vector>

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
 * The merge is the launcher side of the registry domain of a patch layer: the
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

/**
 * @brief Create a hive file which does not exist yet.
 *
 * Mounting a hive which does not exist creates it, which is how the image of
 * the registry gets a file to merge into when the archive carries no registry
 * of its own. An existing file is left untouched, so the helper can run before
 * every merge of a start.
 *
 * @param[in] hive DOS path of the hive.
 * @param[out] error Error description on failure.
 * @return true when the file exists afterwards.
 */
bool EnsureHiveFile(const std::wstring& hive, std::string& error);

/**
 * @brief Apply the isolation modes of a layer to the hive the sandbox mounts.
 *
 * A layer may give an entry a mode which differs from the mode the layer below
 * it packed the content with, and the content of a hidden entry lives in the
 * hidden store of the hive (see `common/RegistryIsolation.hpp`): a package which
 * marks an entry `Hide` hides the content of the layers below it, and a package
 * which gives a hidden entry another mode makes that content visible again. The
 * entries the layer names are moved between the view and the store according to
 * the modes of the layer:
 *
 * * `Hide` copies the entry of the view into the hidden store and removes it
 *   from the view, so the entry disappears from the view of the sandbox while
 *   its content is kept.
 * * Every other mode copies the entry of the hidden store into the view, where
 *   the state of the sandbox wins: a value the view already holds and a sub key
 *   the view already holds are kept, because the writes of the run are the
 *   highest layer of the view.
 *
 * The content of a hidden entry stays in the store, so a mode which is applied
 * and then withdrawn again finds it in place.
 *
 * @param[in] hive DOS path of the hive the sandbox mounts.
 * @param[in] keys The key entries of the isolation file of the layer.
 * @param[in] values The value entries of the isolation file of the layer.
 * @param[out] error Error description on failure.
 * @return true when every named entry was moved.
 */
bool ApplyHiveIsolationModes(const std::wstring& hive, const std::vector<registry_isolation::KeyEntry>& keys,
                             const std::vector<registry_isolation::ValueEntry>& values, std::string& error);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_HIVE_MERGE_HPP
