#ifndef APPBOX_COMMON_EXECUTABLE_PAYLOAD_HPP
#define APPBOX_COMMON_EXECUTABLE_PAYLOAD_HPP

#include "ErrorCode.hpp"
#include <tl/expected.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
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
inline std::array<char, kTrailerSize> BuildTrailer(const PayloadLocation& location) noexcept
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
 * @brief Read the trailer of a self-extracting program.
 *
 * A file which is shorter than the trailer, or whose last bytes do not carry
 * the magic, is an ordinary program: it has no payload, which is the empty
 * optional and not a failure. A file which carries the magic but whose fields
 * do not describe the bytes before the trailer is reported apart from one which
 * cannot be read at all, so a damaged program is neither mistaken for a payload
 * nor for a plain program.
 *
 * @param[in] program Path of the program file.
 * @return The position of the payload, an empty optional for a program which
 *         carries no trailer, or the code of the failure:
 *         `kPayloadTrailerUnreadable` when the file cannot be measured, opened
 *         or read, and `kPayloadTrailerInconsistent` when its trailer does not
 *         hold.
 */
inline tl::expected<std::optional<PayloadLocation>, ErrorCode> ReadTrailer(
    const std::filesystem::path& program) noexcept
{
    std::error_code ec;
    const auto      file_size = std::filesystem::file_size(program, ec);
    if (ec)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPayloadTrailerUnreadable);
    }

    if (file_size < kTrailerSize)
    {
        return std::optional<PayloadLocation>{};
    }

    std::ifstream file(program, std::ios::binary);
    if (!file)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPayloadTrailerUnreadable);
    }

    file.seekg(static_cast<std::streamoff>(file_size - kTrailerSize));
    std::array<char, kTrailerSize> trailer{};
    file.read(trailer.data(), static_cast<std::streamsize>(trailer.size()));
    if (!file)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPayloadTrailerUnreadable);
    }

    if (std::memcmp(trailer.data() + 24, kMagic, kMagicSize) != 0)
    {
        return std::optional<PayloadLocation>{};
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
        return tl::unexpected<ErrorCode>(ErrorCode::kPayloadTrailerInconsistent);
    }

    return std::optional<PayloadLocation>{
        PayloadLocation{ offset, size }
    };
}

} // namespace appbox::executable_payload

#endif // APPBOX_COMMON_EXECUTABLE_PAYLOAD_HPP
