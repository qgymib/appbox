#ifndef APPBOX_COMMON_EXECUTABLE_PAYLOAD_HPP
#define APPBOX_COMMON_EXECUTABLE_PAYLOAD_HPP

#include "WString.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace appbox::executable_payload
{

/**
 * @brief Size of the trailer behind the payload of a self-extracting program.
 *
 * The trailer is the last block of the program file, so a launcher finds the
 * payload by looking at the end of its own image.
 */
inline constexpr std::size_t kTrailerSize = 32;

/** Version of the trailer layout, written by BuildTrailer(). */
inline constexpr std::uint32_t kTrailerVersion = 1;

/** Number of bytes of the magic which closes the trailer. */
inline constexpr std::size_t kMagicSize = 8;

/** Magic of the trailer, stored behind its fields without a terminator. */
inline constexpr char kMagic[kMagicSize] = { 'A', 'P', 'P', 'B', 'O', 'X', 'S', 'F' };

/**
 * @brief Position of the payload inside the program file.
 *
 * The offset is measured from the start of the program and the size is the
 * length of the appended zip archive.
 */
struct PayloadLocation
{
    std::uint64_t offset = 0; ///< Offset of the payload inside the program.
    std::uint64_t size = 0;   ///< Length of the payload in bytes.
};

/**
 * @brief Serialize the trailer of an appended payload.
 *
 * The layout is fixed and little endian:
 *
 * |Offset|Size|Field                    |
 * |------|----|-------------------------|
 * |0     |8   |`uint64` payload offset  |
 * |8     |8   |`uint64` payload size    |
 * |16    |4   |`uint32` trailer version |
 * |20    |4   |`uint32` reserved (`0`)  |
 * |24    |8   |magic `APPBOXSF`         |
 *
 * @param[in] location Position and length of the payload.
 * @return The 32 bytes to write behind the payload.
 */
inline std::array<char, kTrailerSize> BuildTrailer(const PayloadLocation& location)
{
    std::array<char, kTrailerSize> trailer{};
    std::memcpy(trailer.data(), &location.offset, sizeof(location.offset));
    std::memcpy(trailer.data() + 8, &location.size, sizeof(location.size));
    const std::uint32_t version = kTrailerVersion;
    std::memcpy(trailer.data() + 16, &version, sizeof(version));
    const std::uint32_t reserved = 0;
    std::memcpy(trailer.data() + 20, &reserved, sizeof(reserved));
    std::memcpy(trailer.data() + 24, kMagic, kMagicSize);
    return trailer;
}

/**
 * @brief Outcome of reading the trailer of a program file.
 */
enum class TrailerStatus
{
    Absent,    ///< The file carries no payload trailer.
    Present,   ///< The trailer was read and describes the payload.
    Unreadable ///< The file cannot be read, or it ends with an inconsistent trailer.
};

/**
 * @brief Read the trailer of a self-extracting program.
 *
 * A file which is shorter than the trailer, or whose last bytes do not carry
 * the magic, is an ordinary program: it has no payload. A file which carries
 * the magic but whose fields do not describe the bytes before the trailer is
 * reported as unreadable, so a damaged program is not mistaken for a payload.
 *
 * @param[in] program Path of the program file.
 * @param[out] location Position and length of the payload, filled when the
 *             trailer is present.
 * @param[out] error Description of the failure, empty unless the status is
 *             `Unreadable`.
 * @return The status of the trailer.
 */
inline TrailerStatus ReadTrailer(const std::filesystem::path& program, PayloadLocation& location, std::string& error)
{
    error.clear();

    std::error_code ec;
    const auto      file_size = std::filesystem::file_size(program, ec);
    if (ec)
    {
        error = "failed to measure '" + WideToUTF8(program.wstring()) + "'";
        return TrailerStatus::Unreadable;
    }

    if (file_size < kTrailerSize)
    {
        return TrailerStatus::Absent;
    }

    std::ifstream file(program, std::ios::binary);
    if (!file)
    {
        error = "failed to read '" + WideToUTF8(program.wstring()) + "'";
        return TrailerStatus::Unreadable;
    }

    file.seekg(static_cast<std::streamoff>(file_size - kTrailerSize));
    std::array<char, kTrailerSize> trailer{};
    file.read(trailer.data(), static_cast<std::streamsize>(trailer.size()));
    if (!file)
    {
        error = "failed to read '" + WideToUTF8(program.wstring()) + "'";
        return TrailerStatus::Unreadable;
    }

    if (std::memcmp(trailer.data() + 24, kMagic, kMagicSize) != 0)
    {
        return TrailerStatus::Absent;
    }

    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t version = 0;
    std::uint32_t reserved = 0;
    std::memcpy(&offset, trailer.data(), sizeof(offset));
    std::memcpy(&size, trailer.data() + 8, sizeof(size));
    std::memcpy(&version, trailer.data() + 16, sizeof(version));
    std::memcpy(&reserved, trailer.data() + 20, sizeof(reserved));

    const auto payload_end = file_size - kTrailerSize;
    if (version != kTrailerVersion || reserved != 0 || size == 0 || offset > payload_end ||
        size != payload_end - offset)
    {
        error = "the payload trailer of '" + WideToUTF8(program.wstring()) + "' is inconsistent";
        return TrailerStatus::Unreadable;
    }

    location.offset = offset;
    location.size = size;
    return TrailerStatus::Present;
}

} // namespace appbox::executable_payload

#endif // APPBOX_COMMON_EXECUTABLE_PAYLOAD_HPP
