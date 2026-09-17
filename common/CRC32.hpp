#ifndef APPBOX_COMMON_CRC32_HPP
#define APPBOX_COMMON_CRC32_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace appbox
{
namespace detail
{

/**
 * @brief Compute the CRC-32 lookup table at compile time.
 * @return The 256 entry lookup table of the reflected polynomial 0xEDB88320.
 */
constexpr std::array<uint32_t, 256> MakeCrc32Table()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
        {
            c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

/**
 * @brief Precomputed CRC-32 lookup table (reflected polynomial 0xEDB88320).
 */
inline constexpr std::array<uint32_t, 256> kCrc32Table = MakeCrc32Table();

} // namespace detail

/**
 * @brief Incremental CRC-32 checksum (IEEE 802.3, reflected polynomial 0xEDB88320).
 */
struct CRC32
{
    /**
     * @brief Update a CRC-32 checksum with the given data block.
     * @param[in] initial Checksum of the previous blocks, zero for the first one.
     * @param[in] data Pointer to the data block, may be null when size is zero.
     * @param[in] size Size of the data block in bytes.
     * @return The updated checksum.
     */
    static uint32_t Update(uint32_t initial, const void* data, size_t size)
    {
        uint32_t    c = initial ^ 0xFFFFFFFFu;
        const auto* u = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            c = detail::kCrc32Table[(c ^ u[i]) & 0xFFu] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    }
};

} // namespace appbox

#endif // APPBOX_COMMON_CRC32_HPP
