#ifndef APPBOX_UTILS_PAIR_HPP
#define APPBOX_UTILS_PAIR_HPP

#include <stdexcept>

namespace appbox
{

template <typename K, typename V>
struct Pair
{
    K k;
    V v;
};

/**
 * @brief Search key in array.
 * @param[in] pair Pair value array.
 * @param[in] len Array length.
 * @param[in] key The key to search.
 * @param[in] cmp Compare function. Return 0 if match.
 * @return A matched value.
 */
template <typename K, typename V, typename T>
V PairSearchK(const Pair<K, V>* pair, size_t len, const K& key, T cmp)
{
    for (size_t i = 0; i < len; ++i)
    {
        if (cmp(pair[i].k, key) == 0)
        {
            return pair[i].v;
        }
    }
    throw std::invalid_argument("Pair does not exist");
}

/**
 * @brief Search key in array.
 * @param[in] pair Pair value array.
 * @param[in] len Array length.
 * @param[in] key The key to search.
 * @return A matched value.
 */
template <typename K, typename V>
V PairSearchK(const Pair<K, V>* pair, size_t len, const K& key)
{
    for (size_t i = 0; i < len; ++i)
    {
        if (pair[i].k == key)
        {
            return pair[i].v;
        }
    }
    throw std::invalid_argument("Pair does not exist");
}

/**
 * @brief Search value in array.
 * @param[in] pair Pair value array.
 * @param[in] len Array length.
 * @param[in] v The value to search.
 * @param[in] cmp Compare function. Return 0 if match.
 * @return A matched key.
 */
template <typename K, typename V, typename T>
K PairSearchV(const Pair<K, V>* pair, size_t len, const V& v, T cmp)
{
    for (size_t i = 0; i < len; ++i)
    {
        if (cmp(pair[i].v, v) == 0)
        {
            return pair[i].k;
        }
    }
    throw std::invalid_argument("Pair does not exist");
}

/**
 * @brief Search value in array.
 * @param[in] pair Pair value array.
 * @param[in] len Array length.
 * @param[in] v The value to search.
 * @return A matched key.
 */
template <typename K, typename V>
K PairSearchV(const Pair<K, V>* pair, size_t len, const V& v)
{
    for (size_t i = 0; i < len; ++i)
    {
        if (pair[i].v == v)
        {
            return pair[i].k;
        }
    }
    throw std::invalid_argument("Pair does not exist");
}

} // namespace appbox

#endif
