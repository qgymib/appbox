#include "EnumMerge.hpp"

namespace
{

/**
 * @brief Compare two registry names case insensitively.
 *
 * Registry key and value names are case insensitive by definition. The
 * comparison lowers both operands with towlower(), which covers the ASCII
 * range exactly and approximates the rest of Unicode.
 *
 * @param[in] a The first name.
 * @param[in] b The second name.
 * @return Whether the names are equal ignoring the case.
 */
bool RegistryNameEquals(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
    {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (towlower(a[i]) != towlower(b[i]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether the name collection contains the given name.
 * @param[in] names The name collection.
 * @param[in] name The name to look for.
 * @return Whether the collection holds the name, ignoring the case.
 */
bool ContainsName(const std::vector<std::wstring>& names, const std::wstring& name)
{
    for (const auto& candidate : names)
    {
        if (RegistryNameEquals(candidate, name))
        {
            return true;
        }
    }
    return false;
}

} // namespace

size_t appbox::registry::CountMerged(const std::vector<std::wstring>& hive_names,
                                     const std::vector<std::wstring>& real_names)
{
    size_t count = hive_names.size();
    for (const auto& name : real_names)
    {
        if (!ContainsName(hive_names, name))
        {
            ++count;
        }
    }
    return count;
}

bool appbox::registry::MapMergedIndex(const std::vector<std::wstring>& hive_names,
                                      const std::vector<std::wstring>& real_names, size_t merged_index,
                                      EnumLayer& layer, size_t& layer_index)
{
    /* The hive layer occupies the first hive_names.size() merged indices. */
    if (merged_index < hive_names.size())
    {
        layer = EnumLayer::Hive;
        layer_index = merged_index;
        return true;
    }

    /*
     * Walk the real layer and skip every entry whose name is shadowed by the
     * hive layer. The merged index runs out when the remaining real entries
     * cannot fill it anymore.
     */
    size_t offset = merged_index - hive_names.size();
    for (size_t i = 0; i < real_names.size(); ++i)
    {
        if (ContainsName(hive_names, real_names[i]))
        {
            continue;
        }

        if (offset == 0)
        {
            layer = EnumLayer::Real;
            layer_index = i;
            return true;
        }
        --offset;
    }

    return false;
}
