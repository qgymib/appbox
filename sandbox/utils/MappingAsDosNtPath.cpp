#include <windows.h>
#include <cwctype>
#include <vector>
#include <algorithm>
#include "MappingAsDosNtPath.hpp"

struct DriveDeviceMapping
{
    wchar_t      drive;      // C
    std::wstring devicePath; // \Device\HarddiskVolumeX
};

static bool StartsWithI(const std::wstring& s, const std::wstring& prefix)
{
    if (s.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::towlower(s[i]) != std::towlower(prefix[i]))
            return false;
    }

    return true;
}

static bool IsRejectedNonLocalNamespace(const std::wstring& path)
{
    // 明确拒绝的非本地文件系统 / IPC / 网络命名空间。
    // 即使后续逻辑通常也不会映射成功，提前拒绝可读性更好。
    static const wchar_t* kPrefixes[] = {
        L"\\??\\UNC\\",           L"\\Device\\Mup\\",      L"\\Device\\LanmanRedirector\\",
        L"\\Device\\NamedPipe\\", L"\\Device\\Mailslot\\", L"\\??\\PIPE\\",
        L"\\??\\MAILSLOT\\",      L"\\??\\UNC\\",
    };

    for (const auto* prefix : kPrefixes)
    {
        if (StartsWithI(path, prefix))
            return true;
    }

    return false;
}

static bool IsDriveLetter(wchar_t ch)
{
    ch = static_cast<wchar_t>(std::towupper(ch));
    return ch >= L'A' && ch <= L'Z';
}

static bool IsAbsoluteDosNtPath(const std::wstring& path)
{
    // \??\C:\foo
    if (path.size() < 7)
        return false;

    if (!StartsWithI(path, L"\\??\\"))
        return false;

    if (!IsDriveLetter(path[4]))
        return false;

    if (path[5] != L':')
        return false;

    // 拒绝 \??\C:foo 这种 drive-relative 路径
    if (path.size() >= 7 && path[6] == L'\\')
        return true;

    // \??\C: 本身不是绝对路径，拒绝
    return false;
}

static bool IsLocalDriveLetter(wchar_t drive)
{
    if (!IsDriveLetter(drive))
        return false;

    wchar_t root[] = L"C:\\";
    root[0] = static_cast<wchar_t>(std::towupper(drive));

    const UINT type = GetDriveTypeW(root);

    // 明确拒绝网络盘、未知盘。
    // 这里保留 removable / fixed / cdrom / ramdisk，它们都属于本地文件系统语义。
    return type == DRIVE_REMOVABLE || type == DRIVE_FIXED || type == DRIVE_CDROM || type == DRIVE_RAMDISK;
}

static bool MapVolumeGuidNtPathToDosNtPath(const std::wstring& path, std::wstring& mappedPath)
{
    // 目标格式：
    // \??\Volume{GUID}\foo
    //
    // Win32 API 需要：
    // \\?\Volume{GUID}\foo

    constexpr wchar_t kPrefix[] = L"\\??\\Volume{";

    if (!StartsWithI(path, kPrefix))
        return false;

    size_t closeBrace = path.find(L'}', wcslen(kPrefix));
    if (closeBrace == std::wstring::npos)
        return false;

    // 必须是 \??\Volume{GUID}\...
    if (closeBrace + 1 >= path.size() || path[closeBrace + 1] != L'\\')
        return false;

    std::wstring volumeName = path.substr(4, closeBrace + 2 - 4);
    // volumeName 当前形如 Volume{GUID}\

    std::wstring win32VolumeName = L"\\\\?\\";
    win32VolumeName += volumeName;

    // GetVolumePathNamesForVolumeNameW 需要以反斜杠结尾
    if (win32VolumeName.back() != L'\\')
        win32VolumeName += L"\\";

    DWORD required = 0;
    BOOL  ok = GetVolumePathNamesForVolumeNameW(win32VolumeName.c_str(), nullptr, 0, &required);

    if (!ok && GetLastError() != ERROR_MORE_DATA)
        return false;

    if (required == 0)
        return false;

    std::vector<wchar_t> buffer(required + 2);
    ok = GetVolumePathNamesForVolumeNameW(win32VolumeName.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()),
                                          &required);

    if (!ok)
        return false;

    // 返回 MULTI_SZ，可能包含 C:\ 或 C:\Mount\Volume\
        // 这里按需求只接受盘符挂载点。
    wchar_t drive = 0;

    for (const wchar_t* p = buffer.data(); *p; p += wcslen(p) + 1)
    {
        // C:
        if (wcslen(p) == 3 && IsDriveLetter(p[0]) && p[1] == L':' && p[2] == L'\\' && IsLocalDriveLetter(p[0]))
        {
            drive = static_cast<wchar_t>(std::towupper(p[0]));
            break;
        }
    }

    if (drive == 0)
        return false;

    std::wstring suffix = path.substr(closeBrace + 1); // 包含 '\'

    mappedPath.clear();
    mappedPath += L"\\??\\";
    mappedPath += drive;
    mappedPath += L":";
    mappedPath += suffix;

    return true;
}

static bool IsPathPrefixI(const std::wstring& path, const std::wstring& prefix)
{
    if (!StartsWithI(path, prefix))
        return false;

    if (path.size() == prefix.size())
        return true;

    // 避免 \Device\HarddiskVolume1 错误匹配 \Device\HarddiskVolume10
    return path[prefix.size()] == L'\\';
}

static std::wstring TrimTrailingBackslashes(std::wstring s)
{
    while (s.size() > 1 && s.back() == L'\\')
        s.pop_back();
    return s;
}

static bool MapSystemRootPathToDosNtPath(const std::wstring& path, std::wstring& mappedPath)
{
    constexpr wchar_t kSystemRoot[] = L"\\SystemRoot";

    if (!IsPathPrefixI(path, kSystemRoot))
        return false;

    wchar_t windowsDir[MAX_PATH]{};
    UINT    n = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;

    // GetWindowsDirectoryW 返回 C:\Windows
    std::wstring winDir = TrimTrailingBackslashes(windowsDir);

    if (winDir.size() < 3 || !IsDriveLetter(winDir[0]) || winDir[1] != L':' || winDir[2] != L'\\')
    {
        return false;
    }

    if (!IsLocalDriveLetter(winDir[0]))
        return false;

    std::wstring suffix;
    if (path.size() > wcslen(kSystemRoot))
        suffix = path.substr(wcslen(kSystemRoot)); // 包含开头的 '\'

    mappedPath = L"\\??\\";
    mappedPath += winDir;
    mappedPath += suffix;

    return true;
}

static bool QueryDosDeviceTarget(const std::wstring& dosDeviceName, std::wstring& target)
{
    std::vector<wchar_t> buffer(1024);

    for (;;)
    {
        DWORD n = QueryDosDeviceW(dosDeviceName.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));

        if (n != 0)
        {
            // QueryDosDevice 返回 MULTI_SZ；通常第一个就是当前映射。
            target.assign(buffer.data());
            return !target.empty();
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return false;

        buffer.resize(buffer.size() * 2);
    }
}

static std::vector<DriveDeviceMapping> BuildDriveDeviceMappings()
{
    std::vector<DriveDeviceMapping> result;

    wchar_t drives[512]{};
    DWORD   n = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    if (n == 0 || n >= std::size(drives))
        return result;

    for (const wchar_t* p = drives; *p; p += wcslen(p) + 1)
    {
        // p 形如 C:
        if (!IsDriveLetter(p[0]) || p[1] != L':')
            continue;

        wchar_t drive = static_cast<wchar_t>(std::towupper(p[0]));

        if (!IsLocalDriveLetter(drive))
            continue;

        wchar_t deviceName[] = L"C:";
        deviceName[0] = drive;

        std::wstring target;
        if (!QueryDosDeviceTarget(deviceName, target))
            continue;

        target = TrimTrailingBackslashes(target);

        // 正常本地卷一般是 \Device\HarddiskVolumeX。
        // SUBST 盘可能是 \??\C:\dir，这种不是标准设备路径，这里不用于反查。
        if (StartsWithI(target, L"\\Device\\"))
        {
            result.push_back({ drive, target });
        }
    }

    // 最长优先，避免短前缀误匹配
    std::sort(result.begin(), result.end(), [](const DriveDeviceMapping& a, const DriveDeviceMapping& b) {
        return a.devicePath.size() > b.devicePath.size();
    });

    return result;
}

static bool MapDeviceNtPathToDosNtPath(const std::wstring& path, std::wstring& mappedPath)
{
    auto mappings = BuildDriveDeviceMappings();

    for (const auto& m : mappings)
    {
        if (!IsPathPrefixI(path, m.devicePath))
            continue;

        std::wstring suffix;

        if (path.size() > m.devicePath.size())
            suffix = path.substr(m.devicePath.size()); // 包含开头的 '\'

        mappedPath.clear();
        mappedPath += L"\\??\\";
        mappedPath += m.drive;
        mappedPath += L":";

        if (suffix.empty())
            mappedPath += L"\\";
        else
            mappedPath += suffix;

        return true;
    }

    return false;
}

static bool MapGlobalRootPathToDosNtPath(const std::wstring& path, std::wstring& mappedPath)
{
    // 兼容：
    // \??\GLOBALROOT\Device\HarddiskVolumeX\foo
    constexpr wchar_t kGlobalRoot[] = L"\\??\\GLOBALROOT";

    if (!IsPathPrefixI(path, kGlobalRoot))
        return false;

    std::wstring realNtPath = path.substr(wcslen(kGlobalRoot));

    if (realNtPath.empty() || realNtPath[0] != L'\\')
        return false;

    return MapDeviceNtPathToDosNtPath(realNtPath, mappedPath);
}

static bool MappingAsDosNtPathImpl(const std::wstring& path, std::wstring& mapped_path)
{
    if (path.empty())
        return false;

    if (path[0] != L'\\')
        return false;

    if (IsRejectedNonLocalNamespace(path))
        return false;

    // 已经是 \??\C:\foo 形式
    if (IsAbsoluteDosNtPath(path))
    {
        wchar_t drive = static_cast<wchar_t>(std::towupper(path[4]));

        if (!IsLocalDriveLetter(drive))
            return false;

        mapped_path = path;
        mapped_path[4] = drive;
        return true;
    }

    // \??\Volume{GUID}\foo -> \??\D:\foo
    if (MapVolumeGuidNtPathToDosNtPath(path, mapped_path))
        return true;

    // \SystemRoot\System32\ntdll.dll -> \??\C:\Windows\System32\ntdll.dll
    if (MapSystemRootPathToDosNtPath(path, mapped_path))
        return true;

    // \??\GLOBALROOT\Device\HarddiskVolumeX\foo -> \??\C:\foo
    if (MapGlobalRootPathToDosNtPath(path, mapped_path))
        return true;

    // \Device\HarddiskVolumeX\foo -> \??\C:\foo
    if (StartsWithI(path, L"\\Device\\"))
    {
        if (MapDeviceNtPathToDosNtPath(path, mapped_path))
            return true;
    }

    return false;
}

bool appbox::MappingAsDosNtPath(const std::wstring& path, std::wstring& mapped_path)
{
    std::wstring result;
    if (!MappingAsDosNtPathImpl(path, result))
    {
        return false;
    }

    mapped_path = result;
    return true;
}
