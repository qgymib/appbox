#ifndef APPBOX_COMMON_RANDOM_HPP
#define APPBOX_COMMON_RANDOM_HPP

#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Generate a random string.
 * @param[in] length The length of the string.
 * @return The random string.
 */
std::string RandomString(size_t length);

} // namespace appbox

#endif
