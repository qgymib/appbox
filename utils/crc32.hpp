#ifndef APPBOX_UTILS_CRC32_HPP
#define APPBOX_UTILS_CRC32_HPP

#include <cstdint>

namespace appbox
{

/**
 * Computes the CRC32 checksum for the given buffer.
 *
 * This function calculates the CRC32 checksum of the data provided in the
 * buffer. It is a commonly used algorithm for checking data integrity.
 *
 * @param[in] buff A pointer to the buffer containing the data for which the checksum
 *             is to be calculated.
 * @param[in] size The size, in bytes, of the data in the buffer.
 * @return The computed CRC32 checksum as a 32-bit unsigned integer.
 */
uint32_t crc32(const void* buff, size_t size);

}

#endif // APPBOX_UTILS_CRC32_HPP
