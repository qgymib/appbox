#ifndef APPBOX_TRACER_PEIMAGE_HPP
#define APPBOX_TRACER_PEIMAGE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace appbox::tracer
{

/** One export of a PE image. */
struct ExportEntry
{
    std::wstring name;      ///< Exported name.
    std::uint32_t rva = 0;  ///< Implementation offset; 0 for a forwarded export.
    std::wstring forwarder; ///< Forward target `MODULE.Function`; empty for a normal export.
};

/**
 * @brief The parts of a PE image which arming breakpoints needs.
 *
 * The image is parsed from a byte buffer, so the parser has no dependency on
 * the Windows loader and can be unit tested with the system DLLs and with
 * malformed input. Only the section table and the export directory are read.
 */
class PeImage
{
public:
    /**
     * @brief Read and parse an image file.
     *
     * @param[in] path Path of the image.
     * @return The parsed image.
     * @throw std::runtime_error The file can not be read or is not a PE image.
     */
    static PeImage FromFile(const std::filesystem::path& path);

    /**
     * @brief Parse an image from memory.
     *
     * @param[in] buffer Content of the image.
     * @return The parsed image.
     * @throw std::runtime_error The buffer is not a PE image.
     */
    static PeImage FromBuffer(std::vector<std::uint8_t> buffer);

    /** @return The exports of the image, forwarded ones included. */
    const std::vector<ExportEntry>& Exports() const noexcept { return exports_; }

    /** @return The machine type of the COFF header (IMAGE_FILE_MACHINE_*). */
    std::uint16_t Machine() const noexcept { return machine_; }

    /** @return Whether the image is a 32 bit image (PE32 instead of PE32+). */
    bool Is32Bit() const noexcept { return is_32_bit_; }

    /**
     * @brief Report whether an RVA lies in an executable section.
     *
     * Only such an address may carry a breakpoint: writing the trap byte into
     * anything else would corrupt data of the debugged process.
     *
     * @param[in] rva Offset relative to the image base.
     * @return Whether the RVA is executable; false for 0 and for unknown RVAs.
     */
    bool IsExecutable(std::uint32_t rva) const noexcept;

private:
    /** One entry of the section table. */
    struct Section
    {
        std::uint32_t virtual_address = 0;    ///< RVA of the section.
        std::uint32_t virtual_size = 0;       ///< Size of the section in memory.
        std::uint32_t raw_offset = 0;         ///< File offset of the section content.
        std::uint32_t raw_size = 0;           ///< Size of the section content in the file.
        std::uint32_t characteristics = 0;    ///< Section flags (IMAGE_SCN_*).
    };

    /** Parse the buffer which was handed to FromBuffer. */
    void Parse();

    /**
     * @brief Translate an RVA into an offset inside the image buffer.
     *
     * The file offset of a section is not its RVA: a linker may align the file
     * content differently, which is what the 32 bit system DLLs do. Reading an
     * RVA as if it were an offset therefore only works by accident.
     *
     * @param[in] rva Offset relative to the image base.
     * @return Offset of the same byte inside the buffer.
     * @throw std::runtime_error The RVA does not belong to any section.
     */
    std::size_t OffsetOfRva(std::uint32_t rva) const;

    std::vector<std::uint8_t> data_;      ///< Image content.
    std::uint16_t machine_ = 0;           ///< COFF machine type.
    bool is_32_bit_ = false;              ///< Whether the optional header is PE32.
    std::vector<Section> sections_;       ///< Section table.
    std::vector<ExportEntry> exports_;    ///< Export directory content.
};

/**
 * @brief Split a forwarder string into module and function name.
 *
 * @param[in] forwarder String such as `KERNELBASE.GetCommandLineW`.
 * @param[out] module Module name without extension, lowercased.
 * @param[out] function Function name.
 * @return Whether the string has the expected `module.function` form.
 */
bool SplitForwarder(const std::wstring& forwarder, std::wstring& module, std::wstring& function);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_PEIMAGE_HPP
