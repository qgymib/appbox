#include "WinAPI.h" /* Must be first include file */
#include "ErrorCode.hpp"
#include "MappingAsDosNtPath.hpp"
#include "MappingAsSandboxNtPath.hpp"
#include "PathText.hpp"
#include "ResolveCore.hpp"
#include <cstddef>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

/** Drive of the volume the test machine runs Windows from. */
wchar_t SystemDriveLetter()
{
    wchar_t windows_directory[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows_directory, MAX_PATH) == 0)
    {
        return L'C';
    }

    for (std::size_t index = 0; windows_directory[index] != L'\0'; ++index)
    {
        if (windows_directory[index] >= L'A' && windows_directory[index] <= L'Z')
        {
            return windows_directory[index];
        }
    }

    return L'C';
}

/** Path of the view of the root of the system drive. */
std::wstring SystemDriveRootPath()
{
    std::wstring path = L"\\??\\";
    path += SystemDriveLetter();
    path += L":\\";
    return path;
}

} // namespace

/*
 * The directory name of a path.
 */

TEST(Unit_PathText, DirNameReportsAnEmptyPathByItsCode)
{
    const auto directory = appbox::filesystem::DirName(L"");
    ASSERT_FALSE(directory);
    EXPECT_EQ(directory.error(), appbox::ErrorCode::kPathEmpty);
}

TEST(Unit_PathText, DirNameRemovesTheLastComponent)
{
    const auto directory = appbox::filesystem::DirName(L"\\Windows\\System32\\ntdll.dll");
    ASSERT_TRUE(directory);
    EXPECT_EQ(*directory, L"\\Windows\\System32");
}

TEST(Unit_PathText, DirNameDropsTheTrailingSeparators)
{
    const auto directory = appbox::filesystem::DirName(L"\\Windows\\System32\\ntdll.dll\\\\");
    ASSERT_TRUE(directory);
    EXPECT_EQ(*directory, L"\\Windows\\System32");
}

/**
 * @brief A path which is nothing but separators names the root of a volume.
 *
 * The root has no directory above it, which the empty answer records: the
 * caller keeps the root of the volume from the path it passed.
 */
TEST(Unit_PathText, DirNameOfSeparatorsIsEmpty)
{
    const auto directory = appbox::filesystem::DirName(L"\\\\\\");
    ASSERT_TRUE(directory);
    EXPECT_TRUE(directory->empty());
}

/*
 * The separator of an alternate data stream.
 */

TEST(Unit_PathText, StreamNameSeparatorReportsAPathWithoutAStream)
{
    const auto separator = appbox::filesystem::StreamNameSeparator(L"\\dir\\file.txt");
    ASSERT_FALSE(separator);
    EXPECT_EQ(separator.error(), appbox::ErrorCode::kPathNoStreamName);
}

/**
 * @brief The colon of a drive is no stream separator.
 *
 * The component of a drive is a letter followed by its colon and nothing else,
 * which addresses the drive and not the stream of a file named `C`.
 */
TEST(Unit_PathText, DriveComponentCarriesNoStreamName)
{
    const auto separator = appbox::filesystem::StreamNameSeparator(L"\\??\\C:");
    ASSERT_FALSE(separator);
    EXPECT_EQ(separator.error(), appbox::ErrorCode::kPathNoStreamName);
}

TEST(Unit_PathText, StreamNameSeparatorFindsTheColonOfTheLastComponent)
{
    const std::wstring path = L"\\??\\C:\\dir\\file.txt:stream";
    const auto         separator = appbox::filesystem::StreamNameSeparator(path);
    ASSERT_TRUE(separator);
    EXPECT_EQ(path.substr(*separator), L":stream");
}

TEST(Unit_PathText, StreamNameAndEntryPathOfAStream)
{
    const std::wstring path = L"\\??\\C:\\dir\\file.txt:stream";
    EXPECT_TRUE(appbox::filesystem::CarriesStreamName(path));
    EXPECT_EQ(appbox::filesystem::StreamNameOf(path), L"stream");
    EXPECT_EQ(appbox::filesystem::EntryPathOfStream(path), L"\\??\\C:\\dir\\file.txt");
}

TEST(Unit_PathText, StreamNameAndEntryPathWithoutAStream)
{
    const std::wstring path = L"\\??\\C:\\dir\\file.txt";
    EXPECT_FALSE(appbox::filesystem::CarriesStreamName(path));
    EXPECT_TRUE(appbox::filesystem::StreamNameOf(path).empty());
    EXPECT_EQ(appbox::filesystem::EntryPathOfStream(path), path);
}

TEST(Unit_PathText, SequenceKeepsThePrefixesOfThePath)
{
    const auto sequence = appbox::filesystem::Sequence(L"\\Windows\\System32", 0, L"\\", true);
    ASSERT_EQ(sequence.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(sequence[0], L"");
    EXPECT_EQ(sequence[1], L"\\Windows");
    EXPECT_EQ(sequence[2], L"\\Windows\\System32");
}

TEST(Unit_PathText, IsDriveLetterAcceptsLettersAlone)
{
    EXPECT_TRUE(appbox::filesystem::IsDriveLetter(L'C'));
    EXPECT_TRUE(appbox::filesystem::IsDriveLetter(L'z'));
    EXPECT_FALSE(appbox::filesystem::IsDriveLetter(L'1'));
    EXPECT_FALSE(appbox::filesystem::IsDriveLetter(L':'));
}

/*
 * The mapping of a native path onto the DOS style path of a drive.
 */

TEST(Unit_MappingAsDosNtPath, EmptyPathIsReportedByItsCode)
{
    const auto mapped = appbox::MappingAsDosNtPath(L"");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathEmpty);
}

TEST(Unit_MappingAsDosNtPath, RelativePathIsNoPathOfTheView)
{
    const auto mapped = appbox::MappingAsDosNtPath(L"Windows\\notepad.exe");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathNotInViewNamespace);
}

TEST(Unit_MappingAsDosNtPath, NetworkShareIsNoPathOfTheView)
{
    const auto mapped = appbox::MappingAsDosNtPath(L"\\??\\UNC\\server\\share\\file.txt");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathNotInViewNamespace);
}

TEST(Unit_MappingAsDosNtPath, NamedPipeIsNoPathOfTheView)
{
    const auto mapped = appbox::MappingAsDosNtPath(L"\\Device\\NamedPipe\\appbox");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathNotInViewNamespace);
}

/**
 * @brief A drive letter the machine does not carry is no local file system.
 *
 * `Q:` is chosen because a machine which runs the tests has no such volume: the
 * drive type of a missing root is `DRIVE_NO_ROOT_DIR`, which the mapping
 * refuses as a drive of no local file system.
 */
TEST(Unit_MappingAsDosNtPath, DriveWithoutALocalFileSystemIsRefused)
{
    const auto mapped = appbox::MappingAsDosNtPath(L"\\??\\Q:\\dir\\file.txt");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathNotLocalDrive);
}

TEST(Unit_MappingAsDosNtPath, AbsolutePathOfALocalDriveIsUpperCased)
{
    const wchar_t drive = SystemDriveLetter();
    const wchar_t lower = static_cast<wchar_t>(drive - L'A' + L'a');

    std::wstring path = L"\\??\\";
    path += lower;
    path += L":\\Windows";

    const auto mapped = appbox::MappingAsDosNtPath(path);
    ASSERT_TRUE(mapped) << "the system drive has to be a local file system";

    std::wstring expected = L"\\??\\";
    expected += drive;
    expected += L":\\Windows";
    EXPECT_EQ(*mapped, expected);
}

/**
 * @brief A device path of a local volume is the path of its drive.
 *
 * The device map of the system is what tells which drive holds the volume, so
 * the test reads it back through the mapping of the path it built.
 */
TEST(Unit_MappingAsDosNtPath, DevicePathOfALocalVolumeIsMapped)
{
    const std::wstring root = SystemDriveRootPath();

    /* The device path of the volume the drive is mounted on. */
    wchar_t device_name[] = L"C:";
    device_name[0] = SystemDriveLetter();

    std::vector<wchar_t> buffer(1024);
    const DWORD          length = QueryDosDeviceW(device_name, buffer.data(), static_cast<DWORD>(buffer.size()));
    ASSERT_NE(length, 0u);

    std::wstring device_path(buffer.data());
    device_path += L"\\Windows";

    const auto mapped = appbox::MappingAsDosNtPath(device_path);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(*mapped, root + L"Windows");
}

TEST(Unit_MappingAsDosNtPath, UnknownDeviceIsRefused)
{
    const auto mapped = appbox::MappingAsDosNtPath(L"\\Device\\NoSuchDeviceOfAppBox\\dir");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathDeviceUnknown);
}

/*
 * The mapping of a path of the view into the upper layer.
 */

TEST(Unit_MappingAsSandboxNtPath, PathOfALayerIsBuiltFromTheDrive)
{
    const auto mapped = appbox::MappingPathInSandbox(L"\\??\\C:\\Windows\\notepad.exe", L"\\??\\D:\\Overlay");
    ASSERT_TRUE(mapped);
    EXPECT_EQ(*mapped, L"\\??\\D:\\Overlay\\C\\Windows\\notepad.exe");
}

TEST(Unit_MappingAsSandboxNtPath, RootOfADriveKeepsNoTrailingSeparator)
{
    const auto mapped = appbox::MappingPathInSandbox(L"\\??\\C:\\", L"\\??\\D:\\Overlay");
    ASSERT_TRUE(mapped);
    EXPECT_EQ(*mapped, L"\\??\\D:\\Overlay\\C");
}

TEST(Unit_MappingAsSandboxNtPath, GlobalRootNamespaceIsUnderstood)
{
    const auto mapped = appbox::MappingPathInSandbox(L"\\GLOBAL??\\C:\\dir\\file.txt", L"\\??\\D:\\Overlay");
    ASSERT_TRUE(mapped);
    EXPECT_EQ(*mapped, L"\\??\\D:\\Overlay\\C\\dir\\file.txt");
}

TEST(Unit_MappingAsSandboxNtPath, PathOfAnotherNamespaceIsRefused)
{
    const auto mapped = appbox::MappingPathInSandbox(L"C:\\Windows\\notepad.exe", L"\\??\\D:\\Overlay");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathNotDosDrivePath);
}

TEST(Unit_MappingAsSandboxNtPath, RootOfTheLayerHasToBeAPathOfADrive)
{
    const auto mapped = appbox::MappingPathInSandbox(L"\\??\\C:\\Windows", L"Overlay");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathNotDosDrivePath);
}

TEST(Unit_MappingAsSandboxNtPath, ParentReferenceIsCollapsed)
{
    const auto mapped =
        appbox::MappingPathInSandbox(L"\\??\\C:\\dir\\..\\Windows\\.\\notepad.exe", L"\\??\\D:\\Overlay");
    ASSERT_TRUE(mapped);
    EXPECT_EQ(*mapped, L"\\??\\D:\\Overlay\\C\\Windows\\notepad.exe");
}

TEST(Unit_MappingAsSandboxNtPath, ParentReferenceAboveTheRootIsRefused)
{
    const auto mapped = appbox::MappingPathInSandbox(L"\\??\\C:\\..\\Windows", L"\\??\\D:\\Overlay");
    ASSERT_FALSE(mapped);
    EXPECT_EQ(mapped.error(), appbox::ErrorCode::kPathEscapesDriveRoot);
}

/*
 * The codes of this stage occupy the range of the filesystem and path domain
 * and no Win32 code reports one of them.
 */

TEST(Unit_PathErrorCodes, CodesAreInTheFilesystemRange)
{
    const appbox::ErrorCode codes[] = {
        appbox::ErrorCode::kPathEmpty,
        appbox::ErrorCode::kPathNoStreamName,
        appbox::ErrorCode::kPathNotInViewNamespace,
        appbox::ErrorCode::kPathNotLocalDrive,
        appbox::ErrorCode::kPathVolumeUnmounted,
        appbox::ErrorCode::kPathDeviceUnknown,
        appbox::ErrorCode::kPathNotDosDrivePath,
        appbox::ErrorCode::kPathEscapesDriveRoot,
        appbox::ErrorCode::kKnownFolderUnknown,
        appbox::ErrorCode::kKnownFolderUnavailable,
        appbox::ErrorCode::kReparseDataInvalid,
        appbox::ErrorCode::kReparseTargetNotInView,
        appbox::ErrorCode::kReparseDepthExceeded,
    };

    for (const appbox::ErrorCode code : codes)
    {
        const auto value = static_cast<std::int32_t>(code);
        EXPECT_GE(value, 200);
        EXPECT_LE(value, 299);
    }
}

/*
 * The data of a reparse point.
 */

/**
 * @brief Build the data of a mount point with a substitute name.
 * @param[in] substitute Name the link follows, without a null character.
 * @return The data in the layout the file system stores.
 */
std::vector<BYTE> BuildMountPointData(const std::wstring& substitute)
{
    /** Offset of the name buffer, see `common/ResolveCore.hpp`. */
    constexpr std::size_t kNameOffset = appbox::filesystem::kMountPointPathBufferOffset;

    std::vector<BYTE> data(kNameOffset + substitute.size() * sizeof(wchar_t), 0);

    const ULONG  tag = IO_REPARSE_TAG_MOUNT_POINT;
    const USHORT data_length = static_cast<USHORT>(4 * sizeof(USHORT) + substitute.size() * sizeof(wchar_t));

    std::memcpy(data.data(), &tag, sizeof(tag));
    std::memcpy(data.data() + sizeof(ULONG), &data_length, sizeof(data_length));

    auto*        variant = data.data() + appbox::filesystem::kReparseHeaderSize;
    const USHORT substitute_offset = 0;
    const USHORT substitute_length = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const USHORT print_offset = substitute_length;
    const USHORT print_length = 0;

    std::memcpy(variant, &substitute_offset, sizeof(USHORT));
    std::memcpy(variant + sizeof(USHORT), &substitute_length, sizeof(USHORT));
    std::memcpy(variant + 2 * sizeof(USHORT), &print_offset, sizeof(USHORT));
    std::memcpy(variant + 3 * sizeof(USHORT), &print_length, sizeof(USHORT));

    std::memcpy(data.data() + kNameOffset, substitute.data(), substitute.size() * sizeof(wchar_t));
    return data;
}

TEST(Unit_ReparseData, ShortDataIsInvalid)
{
    const std::vector<BYTE> data(4, 0);
    const auto              target = appbox::filesystem::ReadReparseTarget(data);
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error(), appbox::ErrorCode::kReparseDataInvalid);
}

/**
 * @brief A tag the view does not resolve is no target the view can read.
 *
 * Every tag but a mount point and a symbolic link describes the entry itself,
 * so the data of such a tag carries no target the view follows.
 */
TEST(Unit_ReparseData, UnknownTagIsInvalid)
{
    std::vector<BYTE> data(appbox::filesystem::kMountPointPathBufferOffset, 0);
    const ULONG       tag = IO_REPARSE_TAG_CLOUD;
    std::memcpy(data.data(), &tag, sizeof(tag));

    const auto target = appbox::filesystem::ReadReparseTarget(data);
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error(), appbox::ErrorCode::kReparseDataInvalid);
}

TEST(Unit_ReparseData, MountPointCarriesItsSubstituteName)
{
    const auto data = BuildMountPointData(L"\\??\\D:\\target");
    const auto target = appbox::filesystem::ReadReparseTarget(data);
    ASSERT_TRUE(target);
    EXPECT_EQ(target->substitute, L"\\??\\D:\\target");
    EXPECT_FALSE(target->relative);
}

/**
 * @brief A name which the record claims but does not carry is refused.
 *
 * The lengths of the record come from the file system, so they are validated
 * before the name is read: a record which is shorter than the name it announces
 * must not be read past its end.
 */
TEST(Unit_ReparseData, NameBeyondTheRecordIsInvalid)
{
    auto data = BuildMountPointData(L"\\??\\D:\\target");

    /* Announce twice the name the record actually carries. */
    auto*  variant = data.data() + appbox::filesystem::kReparseHeaderSize;
    USHORT substitute_length = 0;
    std::memcpy(&substitute_length, variant + sizeof(USHORT), sizeof(USHORT));
    substitute_length = static_cast<USHORT>(substitute_length * 2);
    std::memcpy(variant + sizeof(USHORT), &substitute_length, sizeof(USHORT));

    const auto target = appbox::filesystem::ReadReparseTarget(data);
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error(), appbox::ErrorCode::kReparseDataInvalid);
}

TEST(Unit_ReparseData, ReparseTagOfShortDataIsZero)
{
    const std::vector<BYTE> data(2, 0xFF);
    EXPECT_EQ(appbox::filesystem::ReparseTagOf(data), 0u);
}

TEST(Unit_ReparseData, OnlyTheTwoRedirectingTagsAreFollowed)
{
    EXPECT_TRUE(appbox::filesystem::IsRedirectingReparseTag(IO_REPARSE_TAG_MOUNT_POINT));
    EXPECT_TRUE(appbox::filesystem::IsRedirectingReparseTag(IO_REPARSE_TAG_SYMLINK));
    EXPECT_FALSE(appbox::filesystem::IsRedirectingReparseTag(IO_REPARSE_TAG_CLOUD));
}

/*
 * The target of a reparse point in the namespace of the view.
 */

TEST(Unit_ReparseTarget, EmptyTargetIsNotInTheView)
{
    const appbox::filesystem::ResolveFs fs;
    const auto                          target = appbox::filesystem::ViewNamespaceTargetCore(L"", fs);
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error().code, appbox::ErrorCode::kReparseTargetNotInView);
    EXPECT_EQ(target.error().status, STATUS_REPARSE_POINT_NOT_RESOLVED);
}

TEST(Unit_ReparseTarget, PathOfAViewIsUnderstood)
{
    const appbox::filesystem::ResolveFs fs;
    const std::wstring                  root = SystemDriveRootPath();

    const auto target = appbox::filesystem::ViewNamespaceTargetCore(root + L"Windows", fs);
    ASSERT_TRUE(target);
    EXPECT_EQ(*target, root + L"Windows");
}

TEST(Unit_ReparseTarget, RelativeTargetOfALinkInTheRootIsNotInTheView)
{
    /*
     * The link is the root of the drive, which has no directory above it, and a
     * relative target needs that directory to be resolved against.
     */
    const appbox::filesystem::ResolveFs fs;
    const std::wstring                  root = SystemDriveRootPath();

    const auto data = BuildMountPointData(L"target");
    const auto target = appbox::filesystem::ReparseTargetToViewPathCore(data, root, fs);
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error().code, appbox::ErrorCode::kReparseTargetNotInView);
}

TEST(Unit_ReparseTarget, DataTheViewDoesNotReadKeepsTheStatusOfTheLayer)
{
    const appbox::filesystem::ResolveFs fs;
    const std::vector<BYTE>             data(4, 0);

    const auto target = appbox::filesystem::ReparseTargetToViewPathCore(data, L"\\??\\C:\\dir\\link", fs);
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error().code, appbox::ErrorCode::kReparseDataInvalid);
    EXPECT_EQ(target.error().status, STATUS_NOT_A_REPARSE_POINT);
}

TEST(Unit_ReparseTarget, LayerPathBelowNoLayerIsNotInTheView)
{
    appbox::filesystem::ResolveFs fs;
    fs.fs_upper = L"\\??\\D:\\Overlay";

    const auto target = appbox::filesystem::RebaseLayerPathToViewCore(fs, L"\\??\\Z:\\other\\file.txt");
    ASSERT_FALSE(target);
    EXPECT_EQ(target.error().code, appbox::ErrorCode::kReparseTargetNotInView);
}

TEST(Unit_ReparseTarget, LayerPathOfTheUpperLayerIsRebased)
{
    appbox::filesystem::ResolveFs fs;
    fs.fs_upper = L"\\??\\D:\\Overlay";

    const auto target = appbox::filesystem::RebaseLayerPathToViewCore(fs, L"\\??\\D:\\Overlay\\C\\Windows");
    ASSERT_TRUE(target);
    EXPECT_EQ(*target, L"\\??\\C:\\Windows");
}

TEST(Unit_PathErrorCodes, NoWin32CodeReportsThem)
{
    const appbox::ErrorCode codes[] = {
        appbox::ErrorCode::kPathEmpty,
        appbox::ErrorCode::kPathNoStreamName,
        appbox::ErrorCode::kPathNotInViewNamespace,
        appbox::ErrorCode::kPathNotLocalDrive,
        appbox::ErrorCode::kPathVolumeUnmounted,
        appbox::ErrorCode::kPathDeviceUnknown,
        appbox::ErrorCode::kPathNotDosDrivePath,
        appbox::ErrorCode::kPathEscapesDriveRoot,
        appbox::ErrorCode::kKnownFolderUnknown,
        appbox::ErrorCode::kKnownFolderUnavailable,
        appbox::ErrorCode::kReparseDataInvalid,
        appbox::ErrorCode::kReparseTargetNotInView,
        appbox::ErrorCode::kReparseDepthExceeded,
    };

    /*
     * A decision of the view about a text is not a failure a Win32 call
     * reports, so the reverse mapping answers the value which means "no Win32
     * code describes this failure".
     */
    for (const appbox::ErrorCode code : codes)
    {
        EXPECT_EQ(appbox::Win32FromErrorCode(code), ERROR_SUCCESS);
    }
}
