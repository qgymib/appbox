#include <gtest/gtest.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include "PeImage.hpp"
#include "utils/ErrorCodeText.hpp"

namespace
{

/** Offset of the DOS header member which names the PE header. */
constexpr std::size_t kPeHeaderOffsetPosition = 0x3CU;

/** Size of the DOS header, which a minimal image has to carry. */
constexpr std::size_t kDosHeaderSize = 0x40U;

/** Size of the COFF header of an image. */
constexpr std::size_t kCoffHeaderSize = 20U;

/** Size of the optional header of a PE32 image which carries one data directory. */
constexpr std::size_t kOptionalHeaderSize32 = 96U;

/** File offset of the content of the single section of a synthetic image. */
constexpr std::size_t kSectionOffset = 0x200U;

/** RVA the single section of a synthetic image is mapped to. */
constexpr std::uint32_t kSectionRva = 0x1000U;

/** Number of data directories a synthetic image declares. */
constexpr std::uint32_t kDataDirectoryCount = 16U;

/** Size of a synthetic image. */
constexpr std::size_t kImageSize = 0x1000U;

/** Size of the content of the single section of a synthetic image. */
constexpr std::uint32_t kSectionRawSize = 0x800U;

/** Size of the mapped range of the single section of a synthetic image. */
constexpr std::uint32_t kSectionVirtualSize = 0x1000U;

/**
 * @brief Write a little endian 16 bit value into a buffer.
 * @param[in,out] data Buffer to write into.
 * @param[in] offset Position of the value.
 * @param[in] value Value to write.
 */
void WriteU16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value)
{
    data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
}

/**
 * @brief Write a little endian 32 bit value into a buffer.
 * @param[in,out] data Buffer to write into.
 * @param[in] offset Position of the value.
 * @param[in] value Value to write.
 */
void WriteU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
{
    data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    data[offset + 2U] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    data[offset + 3U] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
}

/**
 * @brief Copy a text into a buffer, NUL terminated.
 * @param[in,out] data Buffer to write into.
 * @param[in] offset Position of the first character.
 * @param[in] text Text to write.
 */
void WriteText(std::vector<std::uint8_t>& data, std::size_t offset, const char* text)
{
    const std::size_t length = std::strlen(text);
    for (std::size_t index = 0; index < length; ++index)
    {
        data[offset + index] = static_cast<std::uint8_t>(text[index]);
    }
    data[offset + length] = 0U;
}

/**
 * @brief Build a minimal PE32 image with one section and one export.
 *
 * The buffer is built by hand instead of being read from a file, so a case can
 * change exactly one field of it and pin the code the parser answers. The
 * layout is the DOS header, the PE signature, the COFF header, the optional
 * header with one data directory, the section header, and then the content of
 * the section at the offset the section header names.
 */
struct SyntheticImage
{
    std::vector<std::uint8_t> data; ///< The whole buffer.

    /** @return Offset of the optional header inside the buffer. */
    std::size_t OptionalHeader() const
    {
        return kDosHeaderSize + 4U + kCoffHeaderSize;
    }

    /** @return Offset of the section header inside the buffer. */
    std::size_t SectionHeader() const
    {
        return OptionalHeader() + kOptionalHeaderSize32;
    }

    /** @return Offset of the first data directory inside the buffer. */
    std::size_t DataDirectory() const
    {
        return OptionalHeader() + kOptionalHeaderSize32;
    }

    /** @return Offset of the export directory inside the buffer. */
    std::size_t ExportDirectory() const
    {
        return kSectionOffset;
    }
};

/**
 * @brief Build the image a case starts from.
 *
 * @param[in] with_export Whether the image carries an export directory.
 * @return The image.
 */
SyntheticImage MakeImage(bool with_export)
{
    SyntheticImage image;
    image.data.assign(kImageSize, 0U);

    /* DOS header: "MZ" and the offset of the PE header. */
    image.data[0] = static_cast<std::uint8_t>('M');
    image.data[1] = static_cast<std::uint8_t>('Z');
    WriteU32(image.data, kPeHeaderOffsetPosition, kDosHeaderSize);

    /* PE signature. */
    WriteU32(image.data, kDosHeaderSize, 0x00004550U);

    const std::size_t coff = kDosHeaderSize + 4U;
    WriteU16(image.data, coff, 0x014CU); /* machine: an i386 image */
    WriteU16(image.data, coff + 2U, 1U); /* one section */
    WriteU16(image.data, coff + 16U, kOptionalHeaderSize32);

    const std::size_t optional = image.OptionalHeader();
    WriteU16(image.data, optional, 0x010BU); /* PE32 */
    WriteU32(image.data, optional + 92U, kDataDirectoryCount);

    const std::size_t section_header = image.SectionHeader();
    WriteU32(image.data, section_header + 8U, kSectionVirtualSize);
    WriteU32(image.data, section_header + 12U, kSectionRva);
    WriteU32(image.data, section_header + 16U, kSectionRawSize);
    WriteU32(image.data, section_header + 20U, kSectionOffset);
    WriteU32(image.data, section_header + 36U, 0x60000020U); /* code | execute | read */

    if (!with_export)
    {
        return image;
    }

    /*
     * One export named "Foo" which is no forwarder: its RVA points at code. The
     * size of the directory is what tells a forwarder from code, so it has to
     * cover the strings the directory holds.
     */
    WriteU32(image.data, image.DataDirectory(), kSectionRva);
    WriteU32(image.data, image.DataDirectory() + 4U, 0x400U);

    const std::size_t export_directory = image.ExportDirectory();
    WriteU32(image.data, export_directory + 20U, 1U);                  /* NumberOfFunctions */
    WriteU32(image.data, export_directory + 24U, 1U);                  /* NumberOfNames */
    WriteU32(image.data, export_directory + 28U, kSectionRva + 0x40U); /* AddressOfFunctions */
    WriteU32(image.data, export_directory + 32U, kSectionRva + 0x50U); /* AddressOfNames */
    WriteU32(image.data, export_directory + 36U, kSectionRva + 0x60U); /* AddressOfNameOrdinals */

    /*
     * The three arrays and the name live in the same section. The implementation
     * lies past the declared size of the directory, which is what makes it code:
     * an RVA inside that range names a forwarder instead.
     */
    WriteU32(image.data, export_directory + 0x40U, kSectionRva + 0x500U); /* functions[0] */
    WriteU32(image.data, export_directory + 0x50U, kSectionRva + 0x520U); /* names[0] */
    WriteU16(image.data, export_directory + 0x60U, 0U);                   /* ordinals[0] */
    WriteText(image.data, export_directory + 0x520U, "Foo");

    return image;
}

/**
 * @brief RAII helper providing a unique path below the temp directory.
 */
class TempPath
{
public:
    /**
     * @brief Reserve a unique path.
     * @param[in] name Base name of the file.
     */
    explicit TempPath(const std::wstring& name)
    {
        static unsigned counter = 0;
        path_ =
            std::filesystem::temp_directory_path() /
            (L"appbox-pe-" + name + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter));
    }

    /** Remove the file when it was written. */
    ~TempPath()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    /** @return The path of the file. */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

    /**
     * @brief Write a buffer into the file.
     * @param[in] data Content of the file.
     */
    void Write(const std::vector<std::uint8_t>& data) const
    {
        std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Get the image file of the running test executable.
 * @return The path of the executable.
 */
std::filesystem::path SelfImagePath()
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const DWORD          length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

/**
 * @brief Get the image file of a module the running process loaded.
 *
 * @param[in] name File name of the module.
 * @return The path of the module, or an empty path when the module is not
 *         loaded or the system folder does not hold it.
 */
std::filesystem::path SystemModulePath(const wchar_t* name)
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const UINT           length = ::GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }

    return std::filesystem::path(std::wstring(buffer.data(), length)) / name;
}

} // namespace

/**
 * @brief A buffer this case builds itself is a complete image: the parser
 *        reports the machine type, the bit size and the export directory.
 */
TEST(Unit_PeImage, ASyntheticImageIsParsed)
{
    const auto image = appbox::PeImage::FromBuffer(MakeImage(true).data);
    ASSERT_TRUE(image.has_value()) << appbox::test::DescribeErrorCode(image.error());

    EXPECT_EQ(image->Machine(), 0x014CU);
    EXPECT_TRUE(image->Is32Bit());

    const auto& exports = image->Exports();
    ASSERT_EQ(exports.size(), 1U);
    EXPECT_EQ(exports[0].name, L"Foo");
    EXPECT_EQ(exports[0].rva, 0x1500U);
    EXPECT_TRUE(exports[0].forwarder.empty());
    EXPECT_TRUE(image->IsExecutable(exports[0].rva));
}

/**
 * @brief An image without a data directory carries no export, which is a value
 *        and not a failure.
 */
TEST(Unit_PeImage, AnImageWithoutExportsIsParsed)
{
    const auto image = appbox::PeImage::FromBuffer(MakeImage(false).data);
    ASSERT_TRUE(image.has_value()) << appbox::test::DescribeErrorCode(image.error());

    EXPECT_TRUE(image->Exports().empty());
}

/**
 * @brief A forwarded export names another module instead of code, so its RVA is
 *        empty and its forwarder names the two parts of the target.
 */
TEST(Unit_PeImage, AForwardedExportNamesItsTarget)
{
    auto              image = MakeImage(true);
    const std::size_t export_directory = image.ExportDirectory();

    /* The function RVA now points into the export directory, which is the forwarder form. */
    WriteU32(image.data, export_directory + 0x40U, kSectionRva + 0x140U);
    WriteText(image.data, export_directory + 0x140U, "KERNELBASE.Foo");

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_TRUE(parsed) << appbox::test::DescribeErrorCode(parsed.error());

    const auto& exports = parsed->Exports();
    ASSERT_EQ(exports.size(), 1U);
    EXPECT_EQ(exports[0].rva, 0U);
    EXPECT_EQ(exports[0].forwarder, L"KERNELBASE.Foo");

    const auto split = appbox::SplitForwarder(exports[0].forwarder);
    ASSERT_TRUE(split) << appbox::test::DescribeErrorCode(split.error());
    EXPECT_EQ(split->module, L"kernelbase");
    EXPECT_EQ(split->function, L"Foo");
}

/**
 * @brief The parser reads the image of a system module, which is the file the
 *        loader accepted: the bit size describes the running process and the
 *        export directory of the module is not empty.
 */
TEST(Unit_PeImage, ASystemModuleIsParsed)
{
    const auto image = appbox::PeImage::FromFile(SystemModulePath(L"ntdll.dll"));
    ASSERT_TRUE(image.has_value()) << appbox::test::DescribeErrorCode(image.error());

    EXPECT_EQ(image->Is32Bit(), sizeof(void*) == 4);
    ASSERT_FALSE(image->Exports().empty());

    /* A module of the system exports code, so at least one export is executable. */
    bool has_executable = false;
    for (const auto& entry : image->Exports())
    {
        if (entry.forwarder.empty() && image->IsExecutable(entry.rva))
        {
            has_executable = true;
            break;
        }
    }
    EXPECT_TRUE(has_executable) << "no export of the module lies in an executable section";
}

/**
 * @brief The image of the running test executable is read as well; it carries
 *        no export directory, which is a success with no entry.
 */
TEST(Unit_PeImage, TheRunningExecutableIsParsed)
{
    const auto image = appbox::PeImage::FromFile(SelfImagePath());
    ASSERT_TRUE(image.has_value()) << appbox::test::DescribeErrorCode(image.error());

    EXPECT_EQ(image->Is32Bit(), sizeof(void*) == 4);
    EXPECT_TRUE(image->Exports().empty());
}

/**
 * @brief A file which names no image and a file which holds no byte are both
 *        unreadable: the parser has nothing to measure in either of them.
 */
TEST(Unit_PeImage, AMissingOrEmptyFileIsUnreadable)
{
    const TempPath empty(L"empty");
    empty.Write({});

    const auto missing = appbox::PeImage::FromFile(SelfImagePath().parent_path() / L"no-such-appbox-image.exe");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), appbox::ErrorCode::kPeFileUnreadable);

    const auto blank = appbox::PeImage::FromFile(empty.Get());
    ASSERT_FALSE(blank);
    EXPECT_EQ(blank.error(), appbox::ErrorCode::kPeFileUnreadable);
}

/**
 * @brief A buffer which ends before the DOS header names the PE header is cut
 *        short: the parser cannot tell which kind of file it received.
 */
TEST(Unit_PeImage, ABufferShorterThanTheDosHeaderIsTruncated)
{
    const std::vector<std::uint8_t> data(4U, 0U);

    const auto image = appbox::PeImage::FromBuffer(data);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error(), appbox::ErrorCode::kPeImageTruncated);
}

/**
 * @brief A DOS header which names a PE header past the end of the buffer is
 *        truncated as well.
 */
TEST(Unit_PeImage, APeHeaderOutsideTheBufferIsTruncated)
{
    auto data = MakeImage(true).data;
    WriteU32(data, kPeHeaderOffsetPosition, static_cast<std::uint32_t>(data.size()));

    const auto image = appbox::PeImage::FromBuffer(data);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error(), appbox::ErrorCode::kPeImageTruncated);
}

/**
 * @brief A section table which runs past the end of the buffer is truncated:
 *        the header announces more sections than the file holds.
 */
TEST(Unit_PeImage, ASectionTableBeyondTheBufferIsTruncated)
{
    auto data = MakeImage(false).data;

    /* The COFF header announces a section table which the buffer does not hold. */
    WriteU16(data, kDosHeaderSize + 4U + 2U, 4U);
    data.resize(0x100U);

    const auto image = appbox::PeImage::FromBuffer(data);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error(), appbox::ErrorCode::kPeImageTruncated);
}

/**
 * @brief A buffer whose signature is not `PE\0\0` is a file and no image.
 */
TEST(Unit_PeImage, AForeignSignatureIsNoImage)
{
    auto data = MakeImage(true).data;
    WriteU32(data, kDosHeaderSize, 0x0000454EU); /* "NE\0\0" */

    const auto image = appbox::PeImage::FromBuffer(data);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error(), appbox::ErrorCode::kPeNotAnImage);
}

/**
 * @brief A signature of the image with an optional header magic this build does
 *        not know is no image either.
 */
TEST(Unit_PeImage, AnUnknownOptionalHeaderIsNoImage)
{
    auto              data = MakeImage(true).data;
    const std::size_t optional = kDosHeaderSize + 4U + kCoffHeaderSize;
    WriteU16(data, optional, 0x0000U);

    const auto image = appbox::PeImage::FromBuffer(data);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error(), appbox::ErrorCode::kPeNotAnImage);
}

/**
 * @brief A data directory which names no section is a section table which does
 *        not cover the image.
 */
TEST(Unit_PeImage, AnExportDirectoryOutsideEverySectionIsAnInvalidSection)
{
    auto image = MakeImage(true);
    WriteU32(image.data, image.DataDirectory(), 0x9000U);

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kPeInvalidSection);
}

/**
 * @brief A section whose content lies past the end of the buffer names no byte
 *        the parser could read, so the RVA it holds is not usable.
 */
TEST(Unit_PeImage, ASectionBeyondTheBufferIsAnInvalidSection)
{
    auto image = MakeImage(true);

    /* The section keeps its RVA and is mapped, yet its content is not there. */
    WriteU32(image.data, image.SectionHeader() + 20U, 0x800U);
    image.data.resize(0x300U);

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kPeInvalidSection);
}

/**
 * @brief A directory which announces names without naming the arrays which
 *        hold them contradicts itself.
 */
TEST(Unit_PeImage, ADirectoryWithoutItsArraysIsInvalid)
{
    auto image = MakeImage(true);
    WriteU32(image.data, image.ExportDirectory() + 32U, 0U); /* AddressOfNames */

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kPeInvalidExportDirectory);
}

/**
 * @brief The three arrays of the directory are the content of the directory, so
 *        an array which lies outside the image is an invalid directory and not
 *        a section table which is wrong.
 */
TEST(Unit_PeImage, AnArrayOutsideTheImageIsAnInvalidDirectory)
{
    auto image = MakeImage(true);
    WriteU32(image.data, image.ExportDirectory() + 28U, 0x9000U); /* AddressOfFunctions */

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kPeInvalidExportDirectory);
}

/**
 * @brief An export name which points outside the image makes the entry
 *        unreadable, which refuses the whole directory.
 */
TEST(Unit_PeImage, AnExportNameOutsideTheImageIsAnInvalidDirectory)
{
    auto image = MakeImage(true);
    WriteU32(image.data, image.ExportDirectory() + 0x50U, 0x9000U); /* names[0] */

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kPeInvalidExportDirectory);
}

/**
 * @brief A malformed ordinal is skipped, so an export the parser cannot use
 *        does not refuse the exports which are valid.
 */
TEST(Unit_PeImage, AMalformedOrdinalIsSkipped)
{
    auto image = MakeImage(true);
    WriteU16(image.data, image.ExportDirectory() + 0x60U, 7U); /* beyond NumberOfFunctions */

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_TRUE(parsed) << appbox::test::DescribeErrorCode(parsed.error());
    EXPECT_TRUE(parsed->Exports().empty());
}

/**
 * @brief A forwarder string has to name a module and a function; every other
 *        form describes no target.
 */
TEST(Unit_PeImage, AForeignerStringWithoutTwoNamesIsMalformed)
{
    const std::wstring malformed[] = { L"", L"KERNELBASE", L".Foo", L"KERNELBASE." };

    for (const auto& text : malformed)
    {
        const auto split = appbox::SplitForwarder(text);
        ASSERT_FALSE(split);
        EXPECT_EQ(split.error(), appbox::ErrorCode::kForwarderMalformed);
    }
}

/**
 * @brief A forwarder string of the expected form answers its two names, with
 *        the module name folded to lowercase.
 */
TEST(Unit_PeImage, AForeignerStringOfTwoNamesIsSplit)
{
    const auto split = appbox::SplitForwarder(L"KERNELBASE.GetCommandLineW");
    ASSERT_TRUE(split) << appbox::test::DescribeErrorCode(split.error());
    EXPECT_EQ(split->module, L"kernelbase");
    EXPECT_EQ(split->function, L"GetCommandLineW");

    const auto nested = appbox::SplitForwarder(L"api-ms-win-core.Sleep");
    ASSERT_TRUE(nested) << appbox::test::DescribeErrorCode(nested.error());
    EXPECT_EQ(nested->module, L"api-ms-win-core");
    EXPECT_EQ(nested->function, L"Sleep");
}

/**
 * @brief Only an RVA of an executable section may carry a breakpoint, and an
 *        RVA no section covers carries none.
 */
TEST(Unit_PeImage, OnlyAnExecutableRvaIsExecutable)
{
    auto image = MakeImage(true);

    const auto parsed = appbox::PeImage::FromBuffer(image.data);
    ASSERT_TRUE(parsed) << appbox::test::DescribeErrorCode(parsed.error());

    EXPECT_FALSE(parsed->IsExecutable(0U));
    EXPECT_FALSE(parsed->IsExecutable(0x9000U));
    EXPECT_TRUE(parsed->IsExecutable(0x1100U));

    /* The section loses its executable flag, so the same RVA is data. */
    WriteU32(image.data, image.SectionHeader() + 36U, 0x40000040U); /* initialized data | read */
    const auto data_only = appbox::PeImage::FromBuffer(image.data);
    ASSERT_TRUE(data_only) << appbox::test::DescribeErrorCode(data_only.error());
    EXPECT_FALSE(data_only->IsExecutable(0x1100U));
}

/**
 * @brief The codes of the parser name the content of a file, so no Win32 call
 *        reports them and they occupy the range of the PE images.
 */
TEST(Unit_PeImage, TheCodesHaveNoWin32Code)
{
    constexpr appbox::ErrorCode kCodes[] = {
        appbox::ErrorCode::kPeFileUnreadable,
        appbox::ErrorCode::kPeImageTruncated,
        appbox::ErrorCode::kPeNotAnImage,
        appbox::ErrorCode::kPeInvalidSection,
        appbox::ErrorCode::kPeInvalidExportDirectory,
        appbox::ErrorCode::kForwarderMalformed,
    };

    for (const auto code : kCodes)
    {
        const auto value = static_cast<std::int32_t>(code);
        EXPECT_GE(value, 400);
        EXPECT_LE(value, 499);
        EXPECT_EQ(appbox::Win32FromErrorCode(code), ERROR_SUCCESS)
            << "the code " << appbox::test::DescribeErrorCode(code) << " answers a Win32 code";
    }
}
