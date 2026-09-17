#ifndef APPBOX_COMMON_RANDOM_HPP
#define APPBOX_COMMON_RANDOM_HPP

#include <cstddef>
#include <iterator>
#include <random>
#include <string>

namespace appbox
{

/**
 * @brief Generate a random string.
 * @param[in] length The length of the string.
 * @return The random string.
 */
inline std::string RandomString(size_t length)
{
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    /* -2 accounts for the terminating null character of chars. */
    std::random_device                    rd; /* True random seed */
    std::mt19937                          gen(rd());
    std::uniform_int_distribution<size_t> dis(0, std::size(chars) - 2);

    std::string result;
    for (size_t i = 0; i < length; ++i)
    {
        result += chars[dis(gen)];
    }

    return result;
}

} // namespace appbox

#endif // APPBOX_COMMON_RANDOM_HPP
