#include <gtest/gtest.h>
#include "tracer/PeImage.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

/** Machine type of an x64 image (IMAGE_FILE_MACHINE_AMD64). */
constexpr std::uint16_t kMachineAmd64 = 0x8664;

/** Machine type of an x86 image (IMAGE_FILE_MACHINE_I386). */
constexpr std::uint16_t kMachineI386 = 0x014C;

/**
 * @brief Directory which holds the 64 bit system DLLs.
 *
 * @return The system directory of this process.
 */
std::filesystem::path SystemDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const UINT length = ::GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

/**
 * @brief Directory which holds the 32 bit system DLLs.
 *
 * @return The WOW64 directory, or an empty path when it does not exist.
 */
std::filesystem::path Wow64Directory()
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const UINT length = ::GetSystemWow64DirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

/**
 * @brief Find an export of an image.
 *
 * @param[in] image Image to search.
 * @param[in] name Export name to look for.
 * @return The export, or nullptr when the image does not export the name.
 */
const appbox::tracer::ExportEntry* FindExport(const appbox::tracer::PeImage& image,
                                              const std::wstring& name)
{
    for (const auto& entry : image.Exports())
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }

    return nullptr;
}

} // namespace

/**
 * @brief The three traced DLLs are parsed and expose their well known
 *        functions, which is the base of the address based breakpoints.
 */
TEST(TracerPeImage, SystemDllsExposeTheirWellKnownFunctions)
{
    struct Expectation
    {
        const wchar_t* module;
        const wchar_t* function;
    };

    const Expectation expectations[] = {
        {L"ntdll.dll", L"NtCreateFile"},
        {L"kernel32.dll", L"CreateFileW"},
        {L"kernelbase.dll", L"CreateFileW"},
    };

    for (const auto& expectation : expectations)
    {
        const std::filesystem::path path = SystemDirectory() / expectation.module;
        ASSERT_TRUE(std::filesystem::exists(path)) << path.wstring();

        const auto image = appbox::tracer::PeImage::FromFile(path);
        EXPECT_GT(image.Exports().size(), 500U) << expectation.module;
        EXPECT_FALSE(image.Is32Bit()) << expectation.module;
        EXPECT_EQ(image.Machine(), kMachineAmd64) << expectation.module;

        const auto* entry = FindExport(image, expectation.function);
        ASSERT_NE(entry, nullptr) << expectation.module << "!" << expectation.function;
        EXPECT_NE(entry->rva, 0U);
        EXPECT_TRUE(entry->forwarder.empty());
        EXPECT_TRUE(image.IsExecutable(entry->rva));
    }
}

/**
 * @brief A 32 bit image is recognised as well, because the tracer may run a
 *        32 bit program below the 64 bit debugger.
 */
TEST(TracerPeImage, ThirtyTwoBitImagesAreRecognised)
{
    const std::filesystem::path path = Wow64Directory() / L"ntdll.dll";
    if (path.empty() || !std::filesystem::exists(path))
    {
        GTEST_SKIP() << "the WOW64 directory is not available";
    }

    const auto image = appbox::tracer::PeImage::FromFile(path);
    EXPECT_TRUE(image.Is32Bit());
    EXPECT_EQ(image.Machine(), kMachineI386);
    EXPECT_GT(image.Exports().size(), 500U);

    const auto* entry = FindExport(image, L"NtCreateFile");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(image.IsExecutable(entry->rva));
}

/**
 * @brief A forwarded export carries the target of the forward instead of an
 *        address, which is what makes it unusable as a breakpoint location.
 */
TEST(TracerPeImage, ForwardedExportsCarryTheTargetInsteadOfAnAddress)
{
    const auto image =
        appbox::tracer::PeImage::FromFile(SystemDirectory() / L"kernel32.dll");

    std::size_t forwarded = 0;
    for (const auto& entry : image.Exports())
    {
        if (entry.forwarder.empty())
        {
            EXPECT_NE(entry.rva, 0U) << entry.name;
        }
        else
        {
            EXPECT_EQ(entry.rva, 0U) << entry.name;
            ++forwarded;
        }
    }

    /* kernel32 forwards a large part of its surface to kernelbase and ntdll. */
    EXPECT_GT(forwarded, 50U);
}

/**
 * @brief Forwarder strings name the module and the function, both of which the
 *        arming logic needs.
 */
TEST(TracerPeImage, ForwarderStringsSplitIntoModuleAndFunction)
{
    std::wstring module;
    std::wstring function;

    ASSERT_TRUE(appbox::tracer::SplitForwarder(L"KERNELBASE.GetCommandLineW", module, function));
    EXPECT_EQ(module, L"kernelbase");
    EXPECT_EQ(function, L"GetCommandLineW");

    ASSERT_TRUE(appbox::tracer::SplitForwarder(
        L"api-ms-win-core-libraryloader-l1-1-0.AddDllDirectory", module, function));
    EXPECT_EQ(module, L"api-ms-win-core-libraryloader-l1-1-0");
    EXPECT_EQ(function, L"AddDllDirectory");

    EXPECT_FALSE(appbox::tracer::SplitForwarder(L"nodot", module, function));
    EXPECT_FALSE(appbox::tracer::SplitForwarder(L".leading", module, function));
    EXPECT_FALSE(appbox::tracer::SplitForwarder(L"trailing.", module, function));
}

/**
 * @brief Only executable RVAs may carry a breakpoint; everything else is
 *        rejected so that the trap byte can never corrupt data.
 */
TEST(TracerPeImage, OnlyExecutableAddressesAreAccepted)
{
    const auto image = appbox::tracer::PeImage::FromFile(SystemDirectory() / L"ntdll.dll");

    const auto* entry = FindExport(image, L"NtCreateFile");
    ASSERT_NE(entry, nullptr);

    EXPECT_TRUE(image.IsExecutable(entry->rva));
    EXPECT_FALSE(image.IsExecutable(0U));
    EXPECT_FALSE(image.IsExecutable(0xFFFFFFFFU));
}

/**
 * @brief Malformed input is rejected with an exception instead of crashing or
 *        reading outside the buffer.
 */
TEST(TracerPeImage, MalformedInputIsRejected)
{
    EXPECT_THROW(appbox::tracer::PeImage::FromBuffer({}), std::runtime_error);
    EXPECT_THROW(appbox::tracer::PeImage::FromBuffer(std::vector<std::uint8_t>(64U, 0U)),
                 std::runtime_error);

    /* A PE offset which points outside the buffer. */
    std::vector<std::uint8_t> truncated(4096U, 0U);
    truncated[0x3CU] = 0xFFU;
    truncated[0x3DU] = 0xFFU;
    EXPECT_THROW(appbox::tracer::PeImage::FromBuffer(truncated), std::runtime_error);

    /* A valid signature but nothing behind it. */
    std::vector<std::uint8_t> signature_only(4096U, 0U);
    signature_only[0x3CU] = 0x40U;
    signature_only[0x40U] = 'P';
    signature_only[0x41U] = 'E';
    EXPECT_THROW(appbox::tracer::PeImage::FromBuffer(signature_only), std::runtime_error);

    EXPECT_THROW(appbox::tracer::PeImage::FromFile(L"Z:\\appbox\\no\\such\\image.dll"),
                 std::runtime_error);
}
