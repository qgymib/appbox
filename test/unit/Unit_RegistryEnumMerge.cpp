#include "registry/EnumMerge.hpp"
#include <gtest/gtest.h>

using appbox::registry::EnumLayer;

/**
 * @brief A helper which runs MapMergedIndex and returns the layer letter.
 */
static char LayerOf(const std::vector<std::wstring>& hive, const std::vector<std::wstring>& real, size_t index,
                    size_t& layer_index)
{
    EnumLayer layer = EnumLayer::Hive;
    if (!appbox::registry::MapMergedIndex(hive, real, index, layer, layer_index))
    {
        return '.';
    }
    return layer == EnumLayer::Hive ? 'H' : 'R';
}

/**
 * @brief The merged view is the hive layer followed by the real entries.
 */
TEST(UnitRegistryEnumMerge, OrderHiveFirst)
{
    std::vector<std::wstring> hive = {L"A", L"B"};
    std::vector<std::wstring> real = {L"C", L"D"};

    size_t idx = static_cast<size_t>(-1);
    ASSERT_EQ(LayerOf(hive, real, 0, idx), 'H');
    ASSERT_EQ(idx, 0u);
    ASSERT_EQ(LayerOf(hive, real, 1, idx), 'H');
    ASSERT_EQ(idx, 1u);
    ASSERT_EQ(LayerOf(hive, real, 2, idx), 'R');
    ASSERT_EQ(idx, 0u);
    ASSERT_EQ(LayerOf(hive, real, 3, idx), 'R');
    ASSERT_EQ(idx, 1u);
    ASSERT_EQ(LayerOf(hive, real, 4, idx), '.');
}

/**
 * @brief Real entries whose name exists in the hive layer are skipped and the
 *        layer index refers to the position inside the real layer.
 */
TEST(UnitRegistryEnumMerge, DedupHiveWins)
{
    std::vector<std::wstring> hive = {L"Shadow"};
    std::vector<std::wstring> real = {L"Keep", L"Shadow", L"AlsoKeep"};

    size_t idx = static_cast<size_t>(-1);
    ASSERT_EQ(LayerOf(hive, real, 0, idx), 'H');
    ASSERT_EQ(idx, 0u);
    /* Merged 1 -> real layer, first non shadowed entry "Keep" at index 0. */
    ASSERT_EQ(LayerOf(hive, real, 1, idx), 'R');
    ASSERT_EQ(idx, 0u);
    /* Merged 2 -> real layer entry "AlsoKeep" at index 2 (index 1 shadowed). */
    ASSERT_EQ(LayerOf(hive, real, 2, idx), 'R');
    ASSERT_EQ(idx, 2u);
    ASSERT_EQ(LayerOf(hive, real, 3, idx), '.');
}

/**
 * @brief Name comparison is case insensitive in both directions.
 */
TEST(UnitRegistryEnumMerge, CaseInsensitive)
{
    std::vector<std::wstring> hive = {L"key"};
    std::vector<std::wstring> real = {L"KEY", L"Other"};

    size_t idx = static_cast<size_t>(-1);
    /* The real "KEY" is shadowed, only "Other" survives at merged index 1. */
    ASSERT_EQ(LayerOf(hive, real, 1, idx), 'R');
    ASSERT_EQ(idx, 1u);
    ASSERT_EQ(LayerOf(hive, real, 2, idx), '.');
}

/**
 * @brief Empty layers collapse into the other layer.
 */
TEST(UnitRegistryEnumMerge, EmptyLayer)
{
    std::vector<std::wstring> empty;
    std::vector<std::wstring> real = {L"A", L"B"};

    size_t idx = static_cast<size_t>(-1);
    ASSERT_EQ(LayerOf(empty, real, 0, idx), 'R');
    ASSERT_EQ(idx, 0u);
    ASSERT_EQ(LayerOf(real, empty, 1, idx), 'H');
    ASSERT_EQ(idx, 1u);
    ASSERT_EQ(LayerOf(empty, empty, 0, idx), '.');
}

/**
 * @brief A fully shadowed real layer yields only the hive entries.
 */
TEST(UnitRegistryEnumMerge, AllShadowed)
{
    std::vector<std::wstring> hive = {L"a", L"b"};
    std::vector<std::wstring> real = {L"B", L"A"};

    size_t idx = static_cast<size_t>(-1);
    ASSERT_EQ(LayerOf(hive, real, 0, idx), 'H');
    ASSERT_EQ(LayerOf(hive, real, 1, idx), 'H');
    ASSERT_EQ(LayerOf(hive, real, 2, idx), '.');
}

/**
 * @brief The merged count is the hive count plus the non shadowed real count.
 */
TEST(UnitRegistryEnumMerge, CountMerged)
{
    std::vector<std::wstring> empty;
    ASSERT_EQ(appbox::registry::CountMerged(empty, empty), 0u);
    ASSERT_EQ(appbox::registry::CountMerged(empty, {L"A", L"B"}), 2u);
    ASSERT_EQ(appbox::registry::CountMerged({L"A", L"B"}, empty), 2u);
    /* "b" in the hive shadows "B" of the real layer (case insensitive). */
    ASSERT_EQ(appbox::registry::CountMerged({L"a", L"b"}, {L"B", L"C"}), 3u);
    ASSERT_EQ(appbox::registry::CountMerged({L"x"}, {L"x", L"x", L"y"}), 2u);
}

/**
 * @brief The boundary between the layers is exact: the first merged index past
 *        the hive layer is real entry zero unless it is shadowed.
 */
TEST(UnitRegistryEnumMerge, LayerBoundary)
{
    std::vector<std::wstring> hive = {L"Only"};
    std::vector<std::wstring> real = {L"OnlyToo"};

    size_t idx = static_cast<size_t>(-1);
    ASSERT_EQ(LayerOf(hive, real, 0, idx), 'H');
    ASSERT_EQ(LayerOf(hive, real, 1, idx), 'R');
    ASSERT_EQ(idx, 0u);
}
