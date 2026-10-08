#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "utils/LauncherPath.hpp"
#include "utils/ModuleList.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/SandboxDll.hpp"
#include "Test.hpp"
#include "WString.hpp"

namespace
{

/** One entry of the section table of an image. */
struct Section
{
    std::uint32_t virtual_address = 0; ///< Offset of the section relative to the image base.
    std::uint32_t virtual_size = 0;    ///< Size of the section in memory.
    std::uint32_t raw_offset = 0;      ///< Offset of the content of the section inside the file.
    std::uint32_t raw_size = 0;        ///< Size of the content of the section inside the file.
};

/**
 * @brief Read a structure from the content of an image file.
 *
 * The structures of an image are copied out of the byte buffer instead of being
 * read in place: the buffer carries no alignment of its own, and the structures
 * of an image of the other architecture have stricter requirements than the
 * ones of the running one.
 *
 * @param[in] image Content of the image file.
 * @param[in] offset Offset of the structure inside the content.
 * @param[out] value The structure which was read.
 * @return Whether the content carries the whole structure.
 */
template <typename T>
bool ReadStruct(const std::vector<std::uint8_t>& image, std::size_t offset, T& value)
{
    if (offset > image.size() || image.size() - offset < sizeof(T))
    {
        return false;
    }

    std::memcpy(&value, image.data() + offset, sizeof(T));
    return true;
}

/**
 * @brief Translate an RVA of an image into an offset inside its file content.
 *
 * @param[in] sections Section table of the image.
 * @param[in] rva Offset relative to the image base.
 * @param[out] offset Offset of the same byte inside the file content.
 * @return Whether the RVA belongs to a section of the image.
 */
bool OffsetOfRva(const std::vector<Section>& sections, std::uint32_t rva, std::size_t& offset)
{
    for (const auto& section : sections)
    {
        const std::uint32_t size = (section.virtual_size != 0) ? section.virtual_size : section.raw_size;
        if (rva >= section.virtual_address && rva - section.virtual_address < size)
        {
            offset = static_cast<std::size_t>(section.raw_offset) + (rva - section.virtual_address);
            return true;
        }
    }

    return false;
}

/**
 * @brief Read the RVA of the import directory of an optional header.
 *
 * @tparam T Type of the optional header, the 32 bit one or the 64 bit one.
 * @param[in] optional_header The optional header of an image.
 * @return The RVA of the directory, 0 while the header carries no entry for it.
 */
template <typename T>
std::uint32_t ImportDirectoryRva(const T& optional_header)
{
    if (optional_header.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
    {
        return 0;
    }

    return optional_header.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
}

/**
 * @brief Read the names of the modules an image imports.
 *
 * The image is parsed from the content of its file: the products of the build
 * are 32 bit and 64 bit images, so the reader has to know both optional
 * headers, and the image must not be loaded, which would resolve its imports
 * and run its entry point.
 *
 * @param[in] path Path of the image.
 * @param[out] modules Names of the imported modules, as the image spells them.
 * @param[out] error Reason of a failure, in UTF-8.
 * @return Whether the image was read and parsed.
 */
bool ReadImportedModules(const std::wstring& path, std::vector<std::string>& modules, std::string& error)
{
    std::vector<std::uint8_t> image;
    if (appbox::test::ReadFileFull(path, image) != ERROR_SUCCESS || image.empty())
    {
        error = "the image cannot be read";
        return false;
    }

    IMAGE_DOS_HEADER dos_header{};
    if (!ReadStruct(image, 0, dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE)
    {
        error = "the image carries no DOS header";
        return false;
    }

    const auto nt_offset = static_cast<std::size_t>(dos_header.e_lfanew);

    std::uint32_t signature = 0;
    if (!ReadStruct(image, nt_offset, signature) || signature != IMAGE_NT_SIGNATURE)
    {
        error = "the image carries no PE signature";
        return false;
    }

    const std::size_t file_header_offset = nt_offset + sizeof(signature);

    IMAGE_FILE_HEADER file_header{};
    if (!ReadStruct(image, file_header_offset, file_header))
    {
        error = "the image carries no file header";
        return false;
    }

    std::uint16_t magic = 0;
    if (!ReadStruct(image, file_header_offset + sizeof(file_header), magic))
    {
        error = "the image carries no optional header";
        return false;
    }

    std::uint32_t import_rva = 0;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        IMAGE_NT_HEADERS32 nt_headers{};
        if (!ReadStruct(image, nt_offset, nt_headers))
        {
            error = "the 32 bit optional header of the image is truncated";
            return false;
        }

        import_rva = ImportDirectoryRva(nt_headers.OptionalHeader);
    }
    else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        IMAGE_NT_HEADERS64 nt_headers{};
        if (!ReadStruct(image, nt_offset, nt_headers))
        {
            error = "the 64 bit optional header of the image is truncated";
            return false;
        }

        import_rva = ImportDirectoryRva(nt_headers.OptionalHeader);
    }
    else
    {
        error = "the optional header of the image is unknown";
        return false;
    }

    std::vector<Section> sections;
    std::size_t          section_offset = file_header_offset + sizeof(file_header) + file_header.SizeOfOptionalHeader;
    for (std::uint32_t index = 0; index < file_header.NumberOfSections; ++index)
    {
        IMAGE_SECTION_HEADER header{};
        if (!ReadStruct(image, section_offset, header))
        {
            error = "the section table of the image is truncated";
            return false;
        }

        Section section;
        section.virtual_address = header.VirtualAddress;
        section.virtual_size = header.Misc.VirtualSize;
        section.raw_offset = header.PointerToRawData;
        section.raw_size = header.SizeOfRawData;
        sections.push_back(section);

        section_offset += sizeof(header);
    }

    /* An image without an import directory imports nothing at all. */
    if (import_rva == 0)
    {
        return true;
    }

    std::size_t descriptor_offset = 0;
    if (!OffsetOfRva(sections, import_rva, descriptor_offset))
    {
        error = "the import directory of the image is outside of its sections";
        return false;
    }

    while (true)
    {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!ReadStruct(image, descriptor_offset, descriptor))
        {
            error = "the import directory of the image is truncated";
            return false;
        }

        /* The directory is terminated by a descriptor whose members are zero. */
        if (descriptor.Name == 0)
        {
            break;
        }

        std::size_t name_offset = 0;
        if (!OffsetOfRva(sections, descriptor.Name, name_offset))
        {
            error = "the name of an imported module is outside of the sections of the image";
            return false;
        }

        std::string name;
        while (name_offset < image.size() && image[name_offset] != 0)
        {
            name.push_back(static_cast<char>(image[name_offset]));
            ++name_offset;
        }

        if (name.empty())
        {
            error = "the name of an imported module is empty";
            return false;
        }

        modules.push_back(name);
        descriptor_offset += sizeof(descriptor);
    }

    return true;
}

/**
 * @brief Check that an image of the build links the runtime library statically.
 *
 * @param[in] path Path of the image.
 */
void ExpectStaticRuntime(const std::wstring& path)
{
    const auto name = appbox::WideToUTF8(path);

    std::vector<std::string> modules;
    std::string              error;
    ASSERT_TRUE(ReadImportedModules(path, modules, error)) << name << ": " << error;

    /*
     * The reader is only trusted while it reports the modules every product of
     * the build imports: a reader which failed silently would pass the check
     * below with an empty list.
     */
    ASSERT_FALSE(modules.empty()) << name;
    EXPECT_TRUE(appbox::test::HasModule(modules, "kernel32.dll") || appbox::test::HasModule(modules, "ntdll.dll"))
        << name;

    for (const auto& module : modules)
    {
        EXPECT_FALSE(appbox::test::IsVcRuntimeModule(module)) << name << " imports " << module;
    }
}

/**
 * @brief Get the path of the running executable.
 * @return The path of this executable.
 */
std::wstring SelfPath()
{
    std::wstring path(MAX_PATH, L'\0');
    while (true)
    {
        const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size())
        {
            path.resize(length);
            return path;
        }

        path.resize(path.size() * 2);
    }
}

} // namespace

/**
 * Condition:
 * 1. The build produced the packer, the launcher and the two sandbox injection
 *    modules.
 *
 * Expected:
 * 1. Every one of them links the runtime library of the compiler statically, so
 *    its import table names no module of the Visual C++ runtime: the products
 *    run on a machine which carries no Visual C++ Redistributable, and the
 *    injection modules can be injected into an application which does not
 *    depend on it either.
 */
TEST(Unit_StaticRuntime, ProductsImportNoVcRuntime)
{
    if (appbox::test::config.packer_path.empty() || appbox::test::LauncherPath().empty() ||
        appbox::test::Sandbox32DllPath().empty() || appbox::test::Sandbox64DllPath().empty())
    {
        GTEST_SKIP() << "the products of the build are unavailable: pass --packer=, --launcher=, --sandbox32= and "
                        "--sandbox64=";
    }

    ExpectStaticRuntime(appbox::test::config.packer_path);
    ExpectStaticRuntime(appbox::test::LauncherPath());
    ExpectStaticRuntime(appbox::test::Sandbox32DllPath());
    ExpectStaticRuntime(appbox::test::Sandbox64DllPath());
}

/**
 * Condition:
 * 1. The end-to-end cases run this executable as the program inside the
 *    sandbox, so the modules it imports are the ones every sandboxed process of
 *    the suite carries.
 *
 * Expected:
 * 1. The executable links the runtime library of the compiler statically as
 *    well, so the module list the case `E2E_Launcher_NoVcRuntime` reads cannot
 *    carry a runtime module because of the probe itself.
 */
TEST(Unit_StaticRuntime, TestExecutableImportsNoVcRuntime)
{
    ExpectStaticRuntime(SelfPath());
}
