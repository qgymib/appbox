#ifndef APPBOX_COMMON_DETAILS_MAPPING_AS_DOS_NT_PATH_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_MAPPING_AS_DOS_NT_PATH_DETAIL_HPP

/*
 * Implementation of `common/MappingAsDosNtPath.hpp`: the recognition of the
 * namespaces which never name an entry of the view, the lookup of the drive a
 * volume is mounted on and the rebase of a device path onto the letter of its
 * drive. The header is included by the public header and is not meant to be
 * included on its own.
 *
 * The lookup asks the volume manager and the device map of the system, so the
 * implementation needs `<windows.h>`. It takes the header from
 * `common/WString.hpp`, which brings it in with `WIN32_NO_STATUS` defined: a
 * translation unit of the sandbox includes `ntstatus.h` as well, and the
 * STATUS_* macros of winnt.h would clash with the constants of that header.
 */

#include "MappingAsDosNtPath.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace appbox
{
namespace detail
{

/** Namespaces which never name an entry of a local file system. */
inline constexpr const wchar_t* const kDosNtRejectedNamespacePrefixes[] = {
    L"\\??\\UNC\\",           L"\\Device\\Mup\\",      L"\\Device\\LanmanRedirector\\",
    L"\\Device\\NamedPipe\\", L"\\Device\\Mailslot\\", L"\\??\\PIPE\\",
    L"\\??\\MAILSLOT\\",
};

/** One drive of the machine together with the device path of its volume. */
struct DosNtDriveMapping
{
    wchar_t      drive = 0;   ///< Upper case letter of the drive, e.g. `C`.
    std::wstring device_path; ///< Path of the volume, e.g. `\Device\HarddiskVolume3`.
};

/**
 * @brief Whether a text starts with a prefix, ignoring the case.
 * @param[in] text Text to inspect.
 * @param[in] prefix Prefix to compare against, terminated by a null character.
 * @return true when the text starts with the prefix.
 */
inline bool DosNtStartsWithIgnoreCase(const std::wstring& text, const wchar_t* prefix) noexcept
{
    const std::size_t size = std::wcslen(prefix);

    if (text.size() < size)
    {
        return false;
    }

    for (std::size_t index = 0; index < size; ++index)
    {
        if (std::towlower(text[index]) != std::towlower(prefix[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a character can name the drive of a path.
 * @param[in] character Character to inspect.
 * @return true when the character is a letter.
 */
inline bool DosNtIsDriveLetter(wchar_t character) noexcept
{
    const wchar_t upper = static_cast<wchar_t>(std::towupper(character));
    return upper >= L'A' && upper <= L'Z';
}

/**
 * @brief Whether a path belongs to a namespace which names no entry of the view.
 *
 * The namespaces of the table name a share of a network, a pipe or a mailslot,
 * which are other isolation domains of the sandbox: the path is refused up
 * front so that no other rule of the mapping has to consider it.
 *
 * @param[in] path Path to inspect.
 * @return true when the path belongs to one of the rejected namespaces.
 */
inline bool DosNtIsRejectedNamespace(const std::wstring& path) noexcept
{
    for (const wchar_t* prefix : kDosNtRejectedNamespacePrefixes)
    {
        if (DosNtStartsWithIgnoreCase(path, prefix))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Whether a path is an absolute path of a drive in the object namespace.
 *
 * The form is `\??\X:\...`, which is the form the mapping answers in. The
 * separator behind the colon is required: `\??\X:` and `\??\X:foo` address the
 * current directory of the drive and name no entry.
 *
 * @param[in] path Path to inspect.
 * @return true when the path is an absolute path of a drive.
 */
inline bool DosNtIsAbsolutePath(const std::wstring& path) noexcept
{
    /** Number of the characters of the namespace, which is `\??\`. */
    constexpr std::size_t kNamespaceSize = 4;

    /* The namespace, the letter of the drive, its colon and a separator. */
    if (path.size() < kNamespaceSize + 3)
    {
        return false;
    }

    if (!DosNtStartsWithIgnoreCase(path, L"\\??\\"))
    {
        return false;
    }

    if (!DosNtIsDriveLetter(path[kNamespaceSize]) || path[kNamespaceSize + 1] != L':')
    {
        return false;
    }

    return path[kNamespaceSize + 2] == L'\\';
}

/**
 * @brief Whether a drive is a local file system of the machine.
 *
 * A network drive and a drive of an unknown type are refused, because the
 * entries behind them belong to another machine. Removable, fixed, optical and
 * ram disk drives are local file systems and are accepted.
 *
 * @param[in] drive Letter of the drive to inspect.
 * @return true when the drive is a local file system.
 */
inline bool DosNtIsLocalDrive(wchar_t drive) noexcept
{
    if (!DosNtIsDriveLetter(drive))
    {
        return false;
    }

    wchar_t root[] = L"C:\\";
    root[0] = static_cast<wchar_t>(std::towupper(drive));

    const unsigned int type = GetDriveTypeW(root);

    return type == DRIVE_REMOVABLE || type == DRIVE_FIXED || type == DRIVE_CDROM || type == DRIVE_RAMDISK;
}

/**
 * @brief Whether a path starts with a prefix which names whole components.
 *
 * The character behind the prefix has to be a separator, so that
 * `\Device\HarddiskVolume1` never matches `\Device\HarddiskVolume10`.
 *
 * @param[in] path Path to inspect.
 * @param[in] prefix Prefix to compare against, terminated by a null character.
 * @return true when the path starts with the prefix.
 */
inline bool DosNtIsPathPrefix(const std::wstring& path, const wchar_t* prefix) noexcept
{
    if (!DosNtStartsWithIgnoreCase(path, prefix))
    {
        return false;
    }

    const std::size_t size = std::wcslen(prefix);

    if (path.size() == size)
    {
        return true;
    }

    return path[size] == L'\\';
}

/**
 * @brief Remove the separators at the end of a path.
 *
 * The root of a drive keeps its separator, so a path which is the root of a
 * volume is left unchanged.
 *
 * @param[in] path Path to trim.
 * @return The path without the separators at its end.
 */
inline std::wstring DosNtTrimTrailingSeparators(std::wstring path) noexcept
{
    while (path.size() > 1 && path.back() == L'\\')
    {
        path.pop_back();
    }

    return path;
}

/**
 * @brief Map a volume GUID path to the DOS style path of the drive it is on.
 *
 * The volume of `\??\Volume{GUID}\dir\file` is looked up with the Win32 form of
 * its name, `\\?\Volume{GUID}\`, and the mount points the volume reports are
 * searched for the root of a drive: a volume which is mounted in a directory
 * only names no drive, so the call refuses it.
 *
 * @param[in] path Path to map.
 * @return The DOS style NT path of the volume, e.g. `\??\D:\dir\file`, or
 *         `kPathVolumeUnmounted` when the volume is mounted on no local drive
 *         of the machine.
 */
inline tl::expected<std::wstring, ErrorCode> DosNtMapVolumeGuidPath(const std::wstring& path) noexcept
{
    constexpr wchar_t kPrefix[] = L"\\??\\Volume{";

    /** Number of the characters of the prefix above, without its null character. */
    constexpr std::size_t kPrefixSize = std::size(kPrefix) - 1;

    if (!DosNtStartsWithIgnoreCase(path, kPrefix))
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    const std::size_t close_brace = path.find(L'}', kPrefixSize);
    if (close_brace == std::wstring::npos)
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    /* The name of the volume has to be followed by the separator of the path. */
    if (close_brace + 1 >= path.size() || path[close_brace + 1] != L'\\')
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    /* The component of the volume, `Volume{GUID}\`, without the namespace. */
    constexpr std::size_t kVolumeOffset = 4;
    const std::wstring    volume_name = path.substr(kVolumeOffset, close_brace + 2 - kVolumeOffset);

    /* The lookup reads the Win32 form of the name, which ends with a separator. */
    std::wstring win32_volume_name = L"\\\\?\\";
    win32_volume_name += volume_name;

    DWORD required = 0;
    if (!GetVolumePathNamesForVolumeNameW(win32_volume_name.c_str(), nullptr, 0, &required) &&
        GetLastError() != ERROR_MORE_DATA)
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    if (required == 0)
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 2);
    if (!GetVolumePathNamesForVolumeNameW(win32_volume_name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()),
                                          &required))
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    /*
     * The answer is a list of the mount points of the volume, each of them
     * terminated by a null character. Only the root of a drive is accepted, so
     * a volume which is mounted in a directory is refused.
     */
    wchar_t drive = 0;

    for (const wchar_t* mount_point = buffer.data(); *mount_point != L'\0'; mount_point += std::wcslen(mount_point) + 1)
    {
        if (std::wcslen(mount_point) != 3 || !DosNtIsDriveLetter(mount_point[0]) || mount_point[1] != L':' ||
            mount_point[2] != L'\\' || !DosNtIsLocalDrive(mount_point[0]))
        {
            continue;
        }

        drive = static_cast<wchar_t>(std::towupper(mount_point[0]));
        break;
    }

    if (drive == 0)
    {
        return tl::make_unexpected(ErrorCode::kPathVolumeUnmounted);
    }

    std::wstring mapped;
    mapped += L"\\??\\";
    mapped += drive;
    mapped += L':';

    /* The remainder of the path, which starts with a separator. */
    mapped += path.substr(close_brace + 1);

    return mapped;
}

/**
 * @brief Map a path of the Windows directory to the DOS style path of its drive.
 *
 * The kernel names the directory of Windows as `\SystemRoot`, so a path of the
 * loader - `\SystemRoot\System32\ntdll.dll` - is a path of the drive Windows is
 * installed on.
 *
 * @param[in] path Path to map.
 * @return The DOS style NT path of the entry, e.g.
 *         `\??\C:\Windows\System32\ntdll.dll`, or `kPathNotLocalDrive` when the
 *         directory of Windows is not on a local drive this view can key.
 */
inline tl::expected<std::wstring, ErrorCode> DosNtMapSystemRootPath(const std::wstring& path) noexcept
{
    constexpr wchar_t kSystemRoot[] = L"\\SystemRoot";

    /** Number of the characters of the name above, without its null character. */
    constexpr std::size_t kSystemRootSize = std::size(kSystemRoot) - 1;

    if (!DosNtIsPathPrefix(path, kSystemRoot))
    {
        return tl::make_unexpected(ErrorCode::kPathNotLocalDrive);
    }

    wchar_t            windows_directory[MAX_PATH]{};
    const unsigned int length = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return tl::make_unexpected(ErrorCode::kPathNotLocalDrive);
    }

    const std::wstring windows_path = DosNtTrimTrailingSeparators(windows_directory);

    if (windows_path.size() < 3 || !DosNtIsDriveLetter(windows_path[0]) || windows_path[1] != L':' ||
        windows_path[2] != L'\\')
    {
        return tl::make_unexpected(ErrorCode::kPathNotLocalDrive);
    }

    if (!DosNtIsLocalDrive(windows_path[0]))
    {
        return tl::make_unexpected(ErrorCode::kPathNotLocalDrive);
    }

    std::wstring remainder;
    if (path.size() > kSystemRootSize)
    {
        /* The remainder starts with a separator. */
        remainder = path.substr(kSystemRootSize);
    }

    std::wstring mapped = L"\\??\\";
    mapped += windows_path;
    mapped += remainder;

    return mapped;
}

/**
 * @brief Ask the device map of the system for the target of a DOS device.
 *
 * The answer of the query is a list of the targets of the device; the first
 * entry is the one the device currently maps to, which is the volume of a
 * drive, e.g. `\Device\HarddiskVolume3`.
 *
 * @param[in] dos_device_name Name of the device, e.g. `C:`.
 * @return The target of the device, or `kPathDeviceUnknown` when the device
 *         maps to nothing.
 */
inline tl::expected<std::wstring, ErrorCode> DosNtQueryDosDeviceTarget(const std::wstring& dos_device_name) noexcept
{
    /** Capacity of the first buffer of the query, in characters. */
    constexpr std::size_t kInitialCapacity = 1024;

    /** Upper bound of the buffer, so that a failing query cannot grow it forever. */
    constexpr std::size_t kMaximumCapacity = 64 * 1024;

    std::vector<wchar_t> buffer(kInitialCapacity);

    for (;;)
    {
        const DWORD length = QueryDosDeviceW(dos_device_name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length != 0)
        {
            std::wstring target(buffer.data());
            if (target.empty())
            {
                return tl::make_unexpected(ErrorCode::kPathDeviceUnknown);
            }
            return target;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= kMaximumCapacity)
        {
            return tl::make_unexpected(ErrorCode::kPathDeviceUnknown);
        }

        buffer.resize(buffer.size() * 2);
    }
}

/**
 * @brief Collect the local drives of the machine with the path of their volume.
 *
 * The table is built on every call instead of being cached: a drive can be
 * mounted or removed while a process runs, and a stale table would map a path
 * to the volume of another drive.
 *
 * @return The drives, longest device path first, so that the first hit of a
 *         lookup is the most specific one.
 */
inline std::vector<DosNtDriveMapping> DosNtBuildDriveDeviceMappings() noexcept
{
    /** Capacity of the buffer which receives the names of the drives, in characters. */
    constexpr std::size_t kDriveListCapacity = 512;

    std::vector<DosNtDriveMapping> result;

    wchar_t     drives[kDriveListCapacity]{};
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(kDriveListCapacity), drives);
    if (length == 0 || length >= kDriveListCapacity)
    {
        return result;
    }

    /* The answer is a list of the roots, each of them terminated by a null character. */
    for (const wchar_t* root = drives; *root != L'\0'; root += std::wcslen(root) + 1)
    {
        if (!DosNtIsDriveLetter(root[0]) || root[1] != L':')
        {
            continue;
        }

        const wchar_t drive = static_cast<wchar_t>(std::towupper(root[0]));
        if (!DosNtIsLocalDrive(drive))
        {
            continue;
        }

        wchar_t device_name[] = L"C:";
        device_name[0] = drive;

        const auto target = DosNtQueryDosDeviceTarget(device_name);
        if (!target)
        {
            continue;
        }

        const std::wstring device_path = DosNtTrimTrailingSeparators(*target);

        /*
         * A local volume is a device path such as `\Device\HarddiskVolume3`. The
         * target of a substituted drive is a path of another drive instead,
         * which is no device path and is therefore not used for the lookup.
         */
        if (DosNtStartsWithIgnoreCase(device_path, L"\\Device\\"))
        {
            result.push_back({ drive, device_path });
        }
    }

    std::sort(result.begin(), result.end(), [](const DosNtDriveMapping& left, const DosNtDriveMapping& right) {
        return left.device_path.size() > right.device_path.size();
    });

    return result;
}

/**
 * @brief Map a device path to the DOS style path of the drive of its volume.
 *
 * @param[in] path Path to map, e.g. `\Device\HarddiskVolume3\Windows\notepad.exe`.
 * @return The DOS style NT path of the entry, e.g.
 *         `\??\C:\Windows\notepad.exe`, or `kPathDeviceUnknown` when the device
 *         of the path belongs to no local drive of the machine.
 */
inline tl::expected<std::wstring, ErrorCode> DosNtMapDevicePath(const std::wstring& path) noexcept
{
    const std::vector<DosNtDriveMapping> mappings = DosNtBuildDriveDeviceMappings();

    for (const DosNtDriveMapping& mapping : mappings)
    {
        if (!DosNtIsPathPrefix(path, mapping.device_path.c_str()))
        {
            continue;
        }

        std::wstring mapped;
        mapped += L"\\??\\";
        mapped += mapping.drive;
        mapped += L':';

        if (path.size() > mapping.device_path.size())
        {
            /* The remainder starts with a separator. */
            mapped += path.substr(mapping.device_path.size());
        }
        else
        {
            mapped += L'\\';
        }

        return mapped;
    }

    return tl::make_unexpected(ErrorCode::kPathDeviceUnknown);
}

/**
 * @brief Map a path below `GLOBALROOT` to the DOS style path of its drive.
 *
 * The namespace prefixes the device path of an object, so
 * `\??\GLOBALROOT\Device\HarddiskVolume3\dir\file` names the entry the device
 * path alone names.
 *
 * @param[in] path Path to map.
 * @return The DOS style NT path of the entry, or the class of the refusal of
 *         the device path below the namespace.
 */
inline tl::expected<std::wstring, ErrorCode> DosNtMapGlobalRootPath(const std::wstring& path) noexcept
{
    constexpr wchar_t kGlobalRoot[] = L"\\??\\GLOBALROOT";

    /** Number of the characters of the namespace above, without its null character. */
    constexpr std::size_t kGlobalRootSize = std::size(kGlobalRoot) - 1;

    if (!DosNtIsPathPrefix(path, kGlobalRoot))
    {
        return tl::make_unexpected(ErrorCode::kPathDeviceUnknown);
    }

    const std::wstring device_path = path.substr(kGlobalRootSize);
    if (device_path.empty() || device_path[0] != L'\\')
    {
        return tl::make_unexpected(ErrorCode::kPathDeviceUnknown);
    }

    return DosNtMapDevicePath(device_path);
}

/**
 * @brief Map a native path to a DOS style NT path.
 *
 * The forms are tried from the most specific one to the general one, because
 * only one of them matches a path: the namespaces of the volumes, of the
 * Windows directory and of the global root are prefixes of the object
 * namespace, and a device path is the name the file system reports.
 *
 * @param[in] path Native path to map.
 * @return The DOS style NT path of the entry, or the class of the refusal, see
 *         `MappingAsDosNtPath()`.
 */
inline tl::expected<std::wstring, ErrorCode> DosNtMapAsPathImpl(const std::wstring& path) noexcept
{
    if (path.empty())
    {
        return tl::make_unexpected(ErrorCode::kPathEmpty);
    }

    if (path[0] != L'\\')
    {
        /* A relative path names no entry the view can key by a drive. */
        return tl::make_unexpected(ErrorCode::kPathNotInViewNamespace);
    }

    if (DosNtIsRejectedNamespace(path))
    {
        return tl::make_unexpected(ErrorCode::kPathNotInViewNamespace);
    }

    if (DosNtIsAbsolutePath(path))
    {
        /** Position of the letter of the drive inside the namespace `\??\X:\`. */
        constexpr std::size_t kDriveOffset = 4;

        const wchar_t drive = static_cast<wchar_t>(std::towupper(path[kDriveOffset]));
        if (!DosNtIsLocalDrive(drive))
        {
            return tl::make_unexpected(ErrorCode::kPathNotLocalDrive);
        }

        std::wstring mapped = path;
        mapped[kDriveOffset] = drive;
        return mapped;
    }

    if (DosNtStartsWithIgnoreCase(path, L"\\??\\Volume{"))
    {
        return DosNtMapVolumeGuidPath(path);
    }

    if (DosNtIsPathPrefix(path, L"\\SystemRoot"))
    {
        return DosNtMapSystemRootPath(path);
    }

    if (DosNtIsPathPrefix(path, L"\\??\\GLOBALROOT"))
    {
        return DosNtMapGlobalRootPath(path);
    }

    if (DosNtStartsWithIgnoreCase(path, L"\\Device\\"))
    {
        return DosNtMapDevicePath(path);
    }

    /* A path of the object namespace which is no form the view keys. */
    return tl::make_unexpected(ErrorCode::kPathNotInViewNamespace);
}

} // namespace detail

/* Definition of the contract on the declaration. */
inline tl::expected<std::wstring, ErrorCode> MappingAsDosNtPath(const std::wstring& path) noexcept
{
    return detail::DosNtMapAsPathImpl(path);
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_MAPPING_AS_DOS_NT_PATH_DETAIL_HPP
