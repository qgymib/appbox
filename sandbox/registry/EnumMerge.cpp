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

/**
 * @brief Append the names of one layer which no higher layer shadows.
 * @param[in] names The names of the layer.
 * @param[in,out] seen The names the layers above already reported.
 */
void AppendVisible(const std::vector<std::wstring>& names, std::vector<std::wstring>& seen)
{
    for (const auto& name : names)
    {
        if (!ContainsName(seen, name))
        {
            seen.push_back(name);
        }
    }
}

} // namespace

size_t appbox::registry::CountMerged(const std::vector<std::wstring>& upper_names,
                                     const std::vector<std::wstring>& image_names,
                                     const std::vector<std::wstring>& real_names)
{
    std::vector<std::wstring> seen;
    seen.reserve(upper_names.size() + image_names.size() + real_names.size());

    AppendVisible(upper_names, seen);
    AppendVisible(image_names, seen);
    AppendVisible(real_names, seen);

    return seen.size();
}

bool appbox::registry::MapMergedIndex(const std::vector<std::wstring>& upper_names,
                                      const std::vector<std::wstring>& image_names,
                                      const std::vector<std::wstring>& real_names, size_t merged_index,
                                      EnumLayer& layer, size_t& layer_index)
{
    size_t                    offset = merged_index;
    std::vector<std::wstring> seen;

    /*
     * The state hive occupies the first indices of the merged view, the image
     * follows with the names the state hive does not shadow, and the real layer
     * closes the view with the names neither sandbox layer shadows.
     */
    const std::vector<std::wstring>* layers[] = { &upper_names, &image_names, &real_names };
    const EnumLayer                  layer_of[] = { EnumLayer::Upper, EnumLayer::Image, EnumLayer::Real };
    for (size_t current = 0; current < 3; ++current)
    {
        const auto& names = *layers[current];
        for (size_t index = 0; index < names.size(); ++index)
        {
            if (ContainsName(seen, names[index]))
            {
                continue;
            }

            if (offset == 0)
            {
                layer = layer_of[current];
                layer_index = index;
                return true;
            }

            --offset;
            seen.push_back(names[index]);
        }
    }

    return false;
}
