#include "TestBitmap.hpp"
#include <cstddef>
#include <fstream>
#include <vector>

namespace
{

/** Size of the file header of a bitmap file. */
constexpr std::size_t kFileHeaderSize = 14;

/** Size of the information header of a bitmap file. */
constexpr std::size_t kInfoHeaderSize = 40;

/**
 * @brief Store a 32 bit value as a little endian number.
 * @param[in,out] bytes Bytes which receive the number.
 * @param[in] offset Offset of the number inside the bytes.
 * @param[in] value Number to store.
 */
void Put32(std::vector<char>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset + 0] = static_cast<char>(value & 0xFF);
    bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

} // namespace

bool appbox::test::WriteTestBitmap(const std::filesystem::path& path, int width, int height, std::uint8_t blue,
                                   std::uint8_t green, std::uint8_t red)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    const auto pixel_bytes = static_cast<std::uint32_t>(width * height * 4);

    std::vector<char> bytes(kFileHeaderSize + kInfoHeaderSize + pixel_bytes, '\0');

    bytes[0] = 'B';
    bytes[1] = 'M';
    Put32(bytes, 2, static_cast<std::uint32_t>(bytes.size()));
    Put32(bytes, 10, static_cast<std::uint32_t>(kFileHeaderSize + kInfoHeaderSize));

    Put32(bytes, 14, static_cast<std::uint32_t>(kInfoHeaderSize));
    Put32(bytes, 18, static_cast<std::uint32_t>(width));
    Put32(bytes, 22, static_cast<std::uint32_t>(height));
    Put32(bytes, 26, 1);
    Put32(bytes, 28, 32);
    Put32(bytes, 34, pixel_bytes);

    for (std::size_t index = kFileHeaderSize + kInfoHeaderSize; index < bytes.size(); index += 4)
    {
        bytes[index + 0] = static_cast<char>(blue);
        bytes[index + 1] = static_cast<char>(green);
        bytes[index + 2] = static_cast<char>(red);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();

    return file.good();
}
