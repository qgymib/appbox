#ifndef APPBOX_SANDBOX_REGISTRY_ENUMMERGE_HPP
#define APPBOX_SANDBOX_REGISTRY_ENUMMERGE_HPP

#include <string>
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief The layer which holds an entry of a merged enumeration.
 *
 * The view of a sandboxed process is composed of three layers, in this order:
 * the state hive of the sandbox, the read-only image of the registry of the run
 * and the real registry. A name a higher layer reports hides the same name of
 * every layer below it.
 */
enum class EnumLayer
{
    /**
     * @brief The entry lives inside the state hive of the sandbox.
     */
    Upper,

    /**
     * @brief The entry lives inside the read-only image of the run.
     */
    Image,

    /**
     * @brief The entry lives in the real registry.
     */
    Real,
};

/**
 * @brief Map a merged enumeration index onto the layer which holds the entry.
 *
 * The merged view is the state hive followed by the entries of the image which
 * the state hive does not shadow, followed by the entries of the real registry
 * which neither sandbox layer shadows. Names are compared case insensitively
 * (registry key and value names are case insensitive by definition), and the
 * higher layer wins a conflict, so a shadow key hides the entries of the same
 * name of every layer below it.
 *
 * @param[in] upper_names The names of the state hive layer.
 * @param[in] image_names The names of the image layer.
 * @param[in] real_names The names of the real layer.
 * @param[in] merged_index The index inside the merged view.
 * @param[out] layer The layer which holds the entry.
 * @param[out] layer_index The index of the entry inside its layer.
 * @return true when the merged view holds an entry at merged_index, false when
 *         the index is past the end of the merged view (STATUS_NO_MORE_ENTRIES).
 */
bool MapMergedIndex(const std::vector<std::wstring>& upper_names, const std::vector<std::wstring>& image_names,
                    const std::vector<std::wstring>& real_names, size_t merged_index, EnumLayer& layer,
                    size_t& layer_index);

/**
 * @brief Count the entries of the merged view of three name layers.
 *
 * The merged view is defined by MapMergedIndex(): the state hive first, then
 * the entries of the image which the state hive does not shadow, then the
 * entries of the real layer which neither sandbox layer shadows.
 *
 * @param[in] upper_names The names of the state hive layer.
 * @param[in] image_names The names of the image layer.
 * @param[in] real_names The names of the real layer.
 * @return The number of entries in the merged view.
 */
size_t CountMerged(const std::vector<std::wstring>& upper_names, const std::vector<std::wstring>& image_names,
                   const std::vector<std::wstring>& real_names);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ENUMMERGE_HPP
