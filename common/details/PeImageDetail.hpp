#ifndef APPBOX_COMMON_DETAILS_PEIMAGE_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_PEIMAGE_DETAIL_HPP

/*
 * Implementation of `common/PeImage.hpp`: the byte helpers, the constants of
 * the headers and the parser itself. The header is included by the public
 * header and is not meant to be included on its own.
 *
 * Every helper answers a value or a code, so the parser holds neither a
 * `throw` nor a `catch`: a buffer which is no image is a value the caller
 * receives, not an exception it has to expect.
 */

#include "PeImage.hpp"
#include <fstream>
#include <utility>

namespace appbox
{
namespace detail
{

/* Constants from winnt.h, spelled out so that the parser stays independent of
 * the Windows headers (and of the macros they define). */
inline constexpr std::uint32_t kPeSignature = 0x00004550U;       ///< "PE\0\0".
inline constexpr std::uint16_t kOptionalHeader32 = 0x010BU;      ///< PE32.
inline constexpr std::uint16_t kOptionalHeader64 = 0x020BU;      ///< PE32+.
inline constexpr std::uint32_t kSectionExecutable = 0x20000000U; ///< IMAGE_SCN_MEM_EXECUTE.
inline constexpr std::size_t   kSectionHeaderSize = 40U;         ///< Size of IMAGE_SECTION_HEADER.

/** Offset of the DOS header member which names the PE header. */
inline constexpr std::size_t kPeHeaderOffsetPosition = 0x3CU;

/**
 * @brief Report whether a read of `length` bytes at `offset` fits the buffer.
 *
 * The two positions are compared instead of being added, so a position near
 * the end of the address space cannot overflow into a range which looks valid.
 *
 * @param[in] size Size of the buffer.
 * @param[in] offset Offset of the read.
 * @param[in] length Number of bytes the read covers.
 * @return true when the whole read lies inside the buffer.
 */
inline constexpr bool FitsBuffer(std::size_t size, std::size_t offset, std::size_t length) noexcept
{
    return offset <= size && length <= size - offset;
}

/**
 * @brief Read a 16 bit little endian value.
 *
 * @param[in] data Image content.
 * @param[in] offset Offset of the value.
 * @return The value, or `kPeImageTruncated` when the image ends before it.
 */
inline tl::expected<std::uint16_t, ErrorCode> ReadU16(const std::vector<std::uint8_t>& data,
                                                      std::size_t                      offset) noexcept
{
    if (!FitsBuffer(data.size(), offset, 2U))
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeImageTruncated);
    }

    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1U]) << 8));
}

/**
 * @brief Read a 32 bit little endian value.
 *
 * @param[in] data Image content.
 * @param[in] offset Offset of the value.
 * @return The value, or `kPeImageTruncated` when the image ends before it.
 */
inline tl::expected<std::uint32_t, ErrorCode> ReadU32(const std::vector<std::uint8_t>& data,
                                                      std::size_t                      offset) noexcept
{
    if (!FitsBuffer(data.size(), offset, 4U))
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeImageTruncated);
    }

    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1U]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3U]) << 24);
}

/**
 * @brief Read a NUL terminated ASCII string.
 *
 * Export names and forwarder strings are ASCII by definition, so the bytes are
 * widened one by one; a byte which is not ASCII is kept as it is instead of
 * failing, because the string only has to be printed.
 *
 * @param[in] data Image content.
 * @param[in] offset Offset of the first character.
 * @return The string without its terminator, or `kPeImageTruncated` when the
 *         offset lies outside the image.
 */
inline tl::expected<std::wstring, ErrorCode> ReadAsciiString(const std::vector<std::uint8_t>& data,
                                                             std::size_t                      offset) noexcept
{
    if (offset >= data.size())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeImageTruncated);
    }

    std::wstring text;
    for (std::size_t index = offset; index < data.size() && data[index] != 0U; ++index)
    {
        text.push_back(static_cast<wchar_t>(data[index]));
    }

    return text;
}

/**
 * @brief Read the whole file into a buffer.
 *
 * @param[in] path Path of the file.
 * @return The file content, or `kPeFileUnreadable` when the file cannot be
 *         opened, is empty, or cannot be read completely.
 */
inline tl::expected<std::vector<std::uint8_t>, ErrorCode> ReadFileBytes(const std::filesystem::path& path) noexcept
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeFileUnreadable);
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size <= 0)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeFileUnreadable);
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (stream.gcount() != static_cast<std::streamsize>(buffer.size()))
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeFileUnreadable);
    }

    return buffer;
}

} // namespace detail

inline tl::expected<ForwarderName, ErrorCode> SplitForwarder(const std::wstring& forwarder) noexcept
{
    const auto separator = forwarder.find(L'.');
    if (separator == std::wstring::npos || separator == 0 || separator + 1U >= forwarder.size())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kForwarderMalformed);
    }

    ForwarderName name;
    name.module = forwarder.substr(0, separator);
    for (auto& character : name.module)
    {
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }

    name.function = forwarder.substr(separator + 1U);
    return name;
}

inline tl::expected<PeImage, ErrorCode> PeImage::FromFile(const std::filesystem::path& path) noexcept
{
    const auto buffer = detail::ReadFileBytes(path);
    if (!buffer)
    {
        return tl::unexpected<ErrorCode>(buffer.error());
    }

    return FromBuffer(*buffer);
}

inline tl::expected<PeImage, ErrorCode> PeImage::FromBuffer(std::vector<std::uint8_t> buffer) noexcept
{
    PeImage image;
    image.data_ = std::move(buffer);
    const auto parsed = image.Parse();
    if (!parsed)
    {
        return tl::unexpected<ErrorCode>(parsed.error());
    }

    return image;
}

inline tl::expected<void, ErrorCode> PeImage::Parse() noexcept
{
    machine_ = 0;
    is_32_bit_ = false;
    sections_.clear();
    exports_.clear();

    /* The DOS header names the PE header; an image which does not even hold that member is cut short. */
    const auto pe_offset = detail::ReadU32(data_, detail::kPeHeaderOffsetPosition);
    if (!pe_offset)
    {
        return tl::unexpected<ErrorCode>(pe_offset.error());
    }

    const auto signature = detail::ReadU32(data_, *pe_offset);
    if (!signature)
    {
        return tl::unexpected<ErrorCode>(signature.error());
    }
    if (*signature != detail::kPeSignature)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeNotAnImage);
    }

    const std::size_t coff_header = static_cast<std::size_t>(*pe_offset) + 4U;

    const auto machine = detail::ReadU16(data_, coff_header);
    const auto section_count = detail::ReadU16(data_, coff_header + 2U);
    const auto optional_size = detail::ReadU16(data_, coff_header + 16U);
    if (!machine || !section_count || !optional_size)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeImageTruncated);
    }
    machine_ = *machine;

    const std::size_t optional_header = coff_header + 20U;
    const auto        magic = detail::ReadU16(data_, optional_header);
    if (!magic)
    {
        return tl::unexpected<ErrorCode>(magic.error());
    }
    if (*magic != detail::kOptionalHeader32 && *magic != detail::kOptionalHeader64)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeNotAnImage);
    }

    is_32_bit_ = (*magic == detail::kOptionalHeader32);

    /* The export directory is the first data directory. */
    const std::size_t data_directory = optional_header + (is_32_bit_ ? 96U : 112U);
    const auto        export_rva = detail::ReadU32(data_, data_directory);
    const auto        export_size = detail::ReadU32(data_, data_directory + 4U);
    if (!export_rva || !export_size)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeImageTruncated);
    }

    const std::size_t section_table = optional_header + *optional_size;
    for (std::uint16_t index = 0; index < *section_count; ++index)
    {
        const std::size_t entry = section_table + detail::kSectionHeaderSize * index;
        const auto        virtual_size = detail::ReadU32(data_, entry + 8U);
        const auto        virtual_address = detail::ReadU32(data_, entry + 12U);
        const auto        raw_size = detail::ReadU32(data_, entry + 16U);
        const auto        raw_offset = detail::ReadU32(data_, entry + 20U);
        const auto        characteristics = detail::ReadU32(data_, entry + 36U);
        if (!virtual_size || !virtual_address || !raw_size || !raw_offset || !characteristics)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kPeImageTruncated);
        }

        Section section;
        section.virtual_size = *virtual_size;
        section.virtual_address = *virtual_address;
        section.raw_size = *raw_size;
        section.raw_offset = *raw_offset;
        section.characteristics = *characteristics;
        sections_.push_back(section);
    }

    if (*export_rva == 0U)
    {
        return {};
    }

    const auto export_offset = OffsetOfRva(*export_rva);
    if (!export_offset)
    {
        return tl::unexpected<ErrorCode>(export_offset.error());
    }

    const auto count_functions = detail::ReadU32(data_, *export_offset + 20U);
    const auto count_names = detail::ReadU32(data_, *export_offset + 24U);
    const auto address_functions = detail::ReadU32(data_, *export_offset + 28U);
    const auto address_names = detail::ReadU32(data_, *export_offset + 32U);
    const auto address_ordinals = detail::ReadU32(data_, *export_offset + 36U);
    if (!count_functions || !count_names || !address_functions || !address_names || !address_ordinals)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidExportDirectory);
    }

    /*
     * A directory which announces names has to name the three arrays which hold
     * them, or no entry of it can be read: the member is missing rather than
     * empty, which is what tells a damaged directory from one without names.
     */
    if (*count_names != 0U && (*address_names == 0U || *address_ordinals == 0U || *address_functions == 0U))
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidExportDirectory);
    }

    /*
     * The three arrays are the content of the directory, so a position no
     * section covers is a directory which does not hold, not a section table
     * which is wrong: the export range answers its own code from here on.
     */
    const auto names_offset = OffsetOfRva(*address_names);
    const auto ordinals_offset = OffsetOfRva(*address_ordinals);
    const auto functions_offset = OffsetOfRva(*address_functions);
    if (!names_offset || !ordinals_offset || !functions_offset)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidExportDirectory);
    }

    /** Read the NUL terminated string an RVA of the directory names. */
    const auto read_string_at = [this](std::uint32_t rva) noexcept -> tl::expected<std::wstring, ErrorCode> {
        const auto offset = OffsetOfRva(rva);
        if (!offset)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidExportDirectory);
        }

        return detail::ReadAsciiString(data_, *offset);
    };

    exports_.reserve(*count_names);
    for (std::uint32_t index = 0; index < *count_names; ++index)
    {
        const auto name_rva = detail::ReadU32(data_, *names_offset + 4U * index);
        const auto ordinal = detail::ReadU16(data_, *ordinals_offset + 2U * index);
        if (!name_rva || !ordinal)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidExportDirectory);
        }
        if (*ordinal >= *count_functions)
        {
            /* A malformed ordinal is skipped: the remaining exports stay usable. */
            continue;
        }

        const auto function_rva = detail::ReadU32(data_, *functions_offset + 4U * (*ordinal));
        if (!function_rva)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidExportDirectory);
        }

        const auto name = read_string_at(*name_rva);
        if (!name)
        {
            return tl::unexpected<ErrorCode>(name.error());
        }

        ExportEntry entry;
        entry.name = *name;

        /*
         * A forwarded export carries no code: its "RVA" points into the export
         * directory and names the module and the function which implement it.
         */
        if (*export_size != 0U && *function_rva >= *export_rva && *function_rva < *export_rva + *export_size)
        {
            const auto forwarder = read_string_at(*function_rva);
            if (!forwarder)
            {
                return tl::unexpected<ErrorCode>(forwarder.error());
            }
            entry.forwarder = *forwarder;
        }
        else
        {
            entry.rva = *function_rva;
        }

        exports_.push_back(std::move(entry));
    }

    return {};
}

inline tl::expected<std::size_t, ErrorCode> PeImage::OffsetOfRva(std::uint32_t rva) const noexcept
{
    for (const auto& section : sections_)
    {
        /* The mapped size is the larger of both sizes, so a section which is
         * only described in the file is found as well. */
        const std::uint32_t mapped_size =
            section.virtual_size > section.raw_size ? section.virtual_size : section.raw_size;
        if (rva < section.virtual_address || rva >= section.virtual_address + mapped_size)
        {
            continue;
        }

        const std::size_t offset = static_cast<std::size_t>(section.raw_offset) + (rva - section.virtual_address);
        if (!detail::FitsBuffer(data_.size(), offset, 1U))
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidSection);
        }

        return offset;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kPeInvalidSection);
}

inline bool PeImage::IsExecutable(std::uint32_t rva) const noexcept
{
    if (rva == 0U)
    {
        return false;
    }

    for (const auto& section : sections_)
    {
        const std::uint32_t mapped_size =
            section.virtual_size > section.raw_size ? section.virtual_size : section.raw_size;
        if (rva >= section.virtual_address && rva < section.virtual_address + mapped_size)
        {
            if (!detail::FitsBuffer(data_.size(),
                                    static_cast<std::size_t>(section.raw_offset) + (rva - section.virtual_address), 1U))
            {
                return false;
            }

            return (section.characteristics & detail::kSectionExecutable) != 0U;
        }
    }

    return false;
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_PEIMAGE_DETAIL_HPP
