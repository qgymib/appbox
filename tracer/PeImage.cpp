#include "tracer/PeImage.hpp"
#include <fstream>
#include <stdexcept>

namespace appbox::tracer
{
namespace
{

/* Constants from winnt.h, spelled out so that the parser stays independent of
 * the Windows headers (and of the macros they define). */
constexpr std::uint32_t kPeSignature = 0x00004550U;      ///< "PE\0\0".
constexpr std::uint16_t kOptionalHeader32 = 0x010BU;     ///< PE32.
constexpr std::uint16_t kOptionalHeader64 = 0x020BU;     ///< PE32+.
constexpr std::uint32_t kSectionExecutable = 0x20000000U; ///< IMAGE_SCN_MEM_EXECUTE.
constexpr std::size_t kSectionHeaderSize = 40U;          ///< Size of IMAGE_SECTION_HEADER.

/**
 * @brief Read a 16 bit little endian value.
 *
 * @param[in] data Image content.
 * @param[in] offset Offset of the value.
 * @return The value.
 * @throw std::runtime_error The image ends before the value.
 */
std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    if (offset + 2U > data.size())
    {
        throw std::runtime_error("PE image is truncated");
    }

    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1U]) << 8));
}

/**
 * @brief Read a 32 bit little endian value.
 *
 * @param[in] data Image content.
 * @param[in] offset Offset of the value.
 * @return The value.
 * @throw std::runtime_error The image ends before the value.
 */
std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    if (offset + 4U > data.size())
    {
        throw std::runtime_error("PE image is truncated");
    }

    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 8) |
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
 * @return The string without its terminator.
 * @throw std::runtime_error The offset is outside the image.
 */
std::wstring ReadAsciiString(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    if (offset >= data.size())
    {
        throw std::runtime_error("PE image string offset is outside the image");
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
 * @return The file content.
 * @throw std::runtime_error The file can not be read.
 */
std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("the image can not be opened");
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size <= 0)
    {
        throw std::runtime_error("the image is empty");
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (stream.gcount() != static_cast<std::streamsize>(buffer.size()))
    {
        throw std::runtime_error("the image could not be read completely");
    }

    return buffer;
}

} // namespace

bool SplitForwarder(const std::wstring& forwarder, std::wstring& module, std::wstring& function)
{
    const auto separator = forwarder.find(L'.');
    if (separator == std::wstring::npos || separator == 0 || separator + 1U >= forwarder.size())
    {
        return false;
    }

    module = forwarder.substr(0, separator);
    for (auto& character : module)
    {
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }

    function = forwarder.substr(separator + 1U);
    return true;
}

PeImage PeImage::FromFile(const std::filesystem::path& path)
{
    return FromBuffer(ReadFileBytes(path));
}

PeImage PeImage::FromBuffer(std::vector<std::uint8_t> buffer)
{
    PeImage image;
    image.data_ = std::move(buffer);
    image.Parse();
    return image;
}

void PeImage::Parse()
{
    const std::uint32_t pe_offset = ReadU32(data_, 0x3CU);
    if (ReadU32(data_, pe_offset) != kPeSignature)
    {
        throw std::runtime_error("the buffer is not a PE image");
    }

    const std::size_t coff_header = static_cast<std::size_t>(pe_offset) + 4U;
    machine_ = ReadU16(data_, coff_header);
    const std::uint16_t section_count = ReadU16(data_, coff_header + 2U);
    const std::uint16_t optional_size = ReadU16(data_, coff_header + 16U);

    const std::size_t optional_header = coff_header + 20U;
    const std::uint16_t magic = ReadU16(data_, optional_header);
    if (magic != kOptionalHeader32 && magic != kOptionalHeader64)
    {
        throw std::runtime_error("the PE image has an unknown optional header");
    }

    is_32_bit_ = (magic == kOptionalHeader32);

    /* The export directory is the first data directory. */
    const std::size_t data_directory = optional_header + (is_32_bit_ ? 96U : 112U);
    const std::uint32_t export_rva = ReadU32(data_, data_directory);
    const std::uint32_t export_size = ReadU32(data_, data_directory + 4U);

    const std::size_t section_table = optional_header + optional_size;
    for (std::uint16_t index = 0; index < section_count; ++index)
    {
        const std::size_t entry = section_table + kSectionHeaderSize * index;
        Section section;
        section.virtual_size = ReadU32(data_, entry + 8U);
        section.virtual_address = ReadU32(data_, entry + 12U);
        section.raw_size = ReadU32(data_, entry + 16U);
        section.raw_offset = ReadU32(data_, entry + 20U);
        section.characteristics = ReadU32(data_, entry + 36U);
        sections_.push_back(section);
    }

    if (export_rva == 0U)
    {
        return;
    }

    const std::size_t export_offset = OffsetOfRva(export_rva);
    const std::uint32_t count_functions = ReadU32(data_, export_offset + 20U);
    const std::uint32_t count_names = ReadU32(data_, export_offset + 24U);
    const std::uint32_t address_functions = ReadU32(data_, export_offset + 28U);
    const std::uint32_t address_names = ReadU32(data_, export_offset + 32U);
    const std::uint32_t address_ordinals = ReadU32(data_, export_offset + 36U);

    const std::size_t names_offset = OffsetOfRva(address_names);
    const std::size_t ordinals_offset = OffsetOfRva(address_ordinals);
    const std::size_t functions_offset = OffsetOfRva(address_functions);

    exports_.reserve(count_names);
    for (std::uint32_t index = 0; index < count_names; ++index)
    {
        const std::uint32_t name_rva = ReadU32(data_, names_offset + 4U * index);
        const std::uint16_t ordinal = ReadU16(data_, ordinals_offset + 2U * index);
        if (ordinal >= count_functions)
        {
            /* A malformed ordinal is skipped: the remaining exports stay usable. */
            continue;
        }

        const std::uint32_t function_rva = ReadU32(data_, functions_offset + 4U * ordinal);

        ExportEntry entry;
        entry.name = ReadAsciiString(data_, OffsetOfRva(name_rva));

        /*
         * A forwarded export carries no code: its "RVA" points into the export
         * directory and names the module and the function which implement it.
         */
        if (export_size != 0U && function_rva >= export_rva && function_rva < export_rva + export_size)
        {
            entry.forwarder = ReadAsciiString(data_, OffsetOfRva(function_rva));
        }
        else
        {
            entry.rva = function_rva;
        }

        exports_.push_back(std::move(entry));
    }
}

std::size_t PeImage::OffsetOfRva(std::uint32_t rva) const
{
    for (const auto& section : sections_)
    {
        /* The mapped size is the larger of both sizes, so a section which is
         * only described in the file is found as well. */
        const std::uint32_t mapped_size =
            section.virtual_size > section.raw_size ? section.virtual_size : section.raw_size;
        if (rva >= section.virtual_address && rva < section.virtual_address + mapped_size)
        {
            return static_cast<std::size_t>(section.raw_offset) +
                   (rva - section.virtual_address);
        }
    }

    throw std::runtime_error("PE image RVA is outside every section");
}

bool PeImage::IsExecutable(std::uint32_t rva) const noexcept
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
            return (section.characteristics & kSectionExecutable) != 0U;
        }
    }

    return false;
}

} // namespace appbox::tracer
