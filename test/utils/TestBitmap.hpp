#ifndef APPBOX_TEST_UTILS_TEST_BITMAP_HPP
#define APPBOX_TEST_UTILS_TEST_BITMAP_HPP

#include <cstdint>
#include <filesystem>

namespace appbox::test
{

/**
 * @brief Write a 32 bit bitmap file of one colour.
 *
 * The file is written by hand so a case which needs an image does not depend
 * on a checked in binary: the uncompressed bitmap is a format every decoder of
 * Windows reads, and its size is exactly the size of the pixels it carries.
 *
 * @param[in] path Path of the file to write.
 * @param[in] width Width of the image in pixels.
 * @param[in] height Height of the image in pixels.
 * @param[in] blue Blue value of every pixel.
 * @param[in] green Green value of every pixel.
 * @param[in] red Red value of every pixel.
 * @return true when the file was written.
 */
bool WriteTestBitmap(const std::filesystem::path& path, int width, int height, std::uint8_t blue, std::uint8_t green,
                     std::uint8_t red);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TEST_BITMAP_HPP
