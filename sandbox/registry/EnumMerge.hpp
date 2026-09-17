#ifndef APPBOX_SANDBOX_REGISTRY_ENUMMERGE_HPP
#define APPBOX_SANDBOX_REGISTRY_ENUMMERGE_HPP

#include <string>
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief The registry layer which holds an entry of a merged enumeration.
 */
enum class EnumLayer
{
    /**
     * @brief The entry lives inside the sandbox hive.
     */
    Hive,

    /**
     * @brief The entry lives in the real registry.
     */
    Real,
};

/**
 * @brief Map a merged enumeration index onto the layer which holds the entry.
 *
 * The merged view is the hive layer followed by every real layer entry whose
 * name does not exist in the hive layer. Names are compared case insensitively
 * (registry key and value names are case insensitive by definition), and the
 * hive layer wins a conflict, so a shadow key hides the real key of the same
 * name during enumeration.
 *
 * @param[in] hive_names The names of the hive layer entries.
 * @param[in] real_names The names of the real layer entries.
 * @param[in] merged_index The index inside the merged view.
 * @param[out] layer The layer which holds the entry.
 * @param[out] layer_index The index of the entry inside its layer.
 * @return true when the merged view holds an entry at merged_index, false when
 *         the index is past the end of the merged view (STATUS_NO_MORE_ENTRIES).
 */
bool MapMergedIndex(const std::vector<std::wstring>& hive_names, const std::vector<std::wstring>& real_names,
                    size_t merged_index, EnumLayer& layer, size_t& layer_index);

/**
 * @brief Count the entries of the merged view of two name layers.
 *
 * The merged view is defined by MapMergedIndex(): the hive layer followed by
 * every real layer entry which the hive layer does not shadow.
 *
 * @param[in] hive_names The names of the hive layer entries.
 * @param[in] real_names The names of the real layer entries.
 * @return The number of entries in the merged view.
 */
size_t CountMerged(const std::vector<std::wstring>& hive_names, const std::vector<std::wstring>& real_names);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ENUMMERGE_HPP
