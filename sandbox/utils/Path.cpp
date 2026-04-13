#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>
#include <cctype>
#include <cwctype>
#include "Path.hpp"

/**
 * @brief Case insensitive prefix match.
 * @param[in] str String to match
 * @param[in] prefix Prefix string
 * @param[in] prefix_len Prefix string length
 * @return True if str starts with prefix, case insensitive
 */
static bool PathStartsWithI(const std::wstring& str, const wchar_t* prefix, size_t prefix_len)
{
    if (str.size() < prefix_len)
        return false;
    return _wcsnicmp(str.c_str(), prefix, prefix_len) == 0;
}

/**
 * @brief Query the NT device path corresponding to a drive letter, e.g. "C:" ->
 * "\Device\HarddiskVolume3"
 * @param[in] letter Drive letter
 * @param[out] out Output buffer
 * @return True if successful, false otherwise
 */
static bool PathQueryDriveDevice(wchar_t letter, std::wstring& out)
{
    wchar_t drive[3] = { letter, L':', L'\0' };
    wchar_t buf[MAX_PATH]{};

    /*
     * QueryDosDeviceW returns a multi-string, take the first one.
     */
    if (QueryDosDeviceW(drive, buf, MAX_PATH) > 0)
    {
        out.assign(buf);
        return true;
    }
    return false;
}

std::wstring appbox::DosPathToNt(const std::wstring& dos_path)
{
    if (dos_path.empty())
        return {};

    // -----------------------------------------------------------
    // 1. \\?\ 或 \\.\ 扩展前缀
    // -----------------------------------------------------------
    if (dos_path.size() >= 4 && dos_path[0] == L'\\' && dos_path[1] == L'\\' &&
        (dos_path[2] == L'?' || dos_path[2] == L'.') && dos_path[3] == L'\\')
    {
        std::wstring rest = dos_path.substr(4);

        // \\?\UNC\server\share\... → \Device\Mup\server\share\...
        if (PathStartsWithI(rest, L"UNC\\", 4) && rest.size() > 4)
        {
            return L"\\Device\\Mup\\" + rest.substr(4);
        }

        // \\?\C:\... → 解析盘符
        if (rest.size() >= 2 && std::iswalpha(rest[0]) && rest[1] == L':')
        {
            std::wstring device;
            if (PathQueryDriveDevice(rest[0], device))
            {
                return device + rest.substr(2);
            }
        }

        // 其他情况 \\?\xxx → \??\xxx（NT 对象管理器等价形式）
        return L"\\??\\" + rest;
    }

    // -----------------------------------------------------------
    // 2. UNC 路径 \\server\share\...
    // -----------------------------------------------------------
    if (dos_path.size() >= 2 && dos_path[0] == L'\\' && dos_path[1] == L'\\')
    {
        return L"\\Device\\Mup\\" + dos_path.substr(2);
    }

    // -----------------------------------------------------------
    // 3. 盘符路径 X:\...
    // -----------------------------------------------------------
    if (dos_path.size() >= 2 && std::iswalpha(dos_path[0]) && dos_path[1] == L':')
    {
        std::wstring device;
        if (PathQueryDriveDevice(dos_path[0], device))
        {
            if (dos_path.size() > 2)
                device += dos_path.substr(2); // 含 '\' 及后续部分
            return device;
        }
    }

    // 无法识别，原样返回
    return dos_path;
}

std::wstring appbox::NtPathToDos(const std::wstring& nt_path)
{
    if (nt_path.empty())
        return {};

    // -----------------------------------------------------------
    // 1. \??\ 前缀（Per-session DOS 设备目录）
    // -----------------------------------------------------------
    if (PathStartsWithI(nt_path, L"\\??\\", 4))
    {
        std::wstring rest = nt_path.substr(4);
        // \??\C:\... → C:\...
        if (rest.size() >= 2 && std::iswalpha(rest[0]) && rest[1] == L':')
            return rest;
        // \??\UNC\server\share → \\server\share
        if (PathStartsWithI(rest, L"UNC\\", 4) && rest.size() > 4)
            return L"\\\\" + rest.substr(4);
    }

    // -----------------------------------------------------------
    // 2. \GLOBAL??\ 前缀（全局 DOS 设备目录）
    // -----------------------------------------------------------
    if (PathStartsWithI(nt_path, L"\\GLOBAL??\\", 10))
    {
        std::wstring rest = nt_path.substr(10);
        if (rest.size() >= 2 && std::iswalpha(rest[0]) && rest[1] == L':')
            return rest;
        if (PathStartsWithI(rest, L"UNC\\", 4) && rest.size() > 4)
            return L"\\\\" + rest.substr(4);
    }

    // -----------------------------------------------------------
    // 3. \Device\Mup\ → UNC
    // -----------------------------------------------------------
    constexpr wchar_t kMup[] = L"\\Device\\Mup\\";
    constexpr size_t  kMupLen = sizeof(kMup) / sizeof(wchar_t) - 1; // 12
    if (PathStartsWithI(nt_path, kMup, kMupLen))
    {
        return L"\\\\" + nt_path.substr(kMupLen);
    }

    // -----------------------------------------------------------
    // 4. \Device\LanmanRedirector\ → UNC
    //    格式: \Device\LanmanRedirector\;X:00000000001a2b3c\server\share
    //    需要跳过 ";X:hex" 部分
    // -----------------------------------------------------------
    constexpr wchar_t kLanman[] = L"\\Device\\LanmanRedirector\\";
    constexpr size_t  kLanmanLen = sizeof(kLanman) / sizeof(wchar_t) - 1; // 25
    if (PathStartsWithI(nt_path, kLanman, kLanmanLen))
    {
        // 如果紧跟 ';'，则跳过 ;X:session_id 直到下一个 '\'
        size_t search_start = kLanmanLen;
        if (search_start < nt_path.size() && nt_path[search_start] == L';')
        {
            size_t sep = nt_path.find(L'\\', search_start);
            if (sep != std::wstring::npos && sep + 1 < nt_path.size())
                return L"\\\\" + nt_path.substr(sep + 1);
        }
        // 没有 ';' 部分，直接取剩余
        if (kLanmanLen < nt_path.size())
            return L"\\\\" + nt_path.substr(kLanmanLen);
    }

    // -----------------------------------------------------------
    // 5. \Device\WebDavRedirector\ → UNC（格式类似 LanmanRedirector）
    // -----------------------------------------------------------
    constexpr wchar_t kWebDav[] = L"\\Device\\WebDavRedirector\\";
    constexpr size_t  kWebDavLen = sizeof(kWebDav) / sizeof(wchar_t) - 1;
    if (PathStartsWithI(nt_path, kWebDav, kWebDavLen))
    {
        size_t search_start = kWebDavLen;
        if (search_start < nt_path.size() && nt_path[search_start] == L';')
        {
            size_t sep = nt_path.find(L'\\', search_start);
            if (sep != std::wstring::npos && sep + 1 < nt_path.size())
                return L"\\\\" + nt_path.substr(sep + 1);
        }
        if (kWebDavLen < nt_path.size())
            return L"\\\\" + nt_path.substr(kWebDavLen);
    }

    // -----------------------------------------------------------
    // 6. 枚举所有盘符，匹配 \Device\HarddiskVolumeN 等设备路径
    // -----------------------------------------------------------
    wchar_t drives_buf[512]{};
    DWORD   len = GetLogicalDriveStringsW(static_cast<DWORD>(_countof(drives_buf)), drives_buf);

    if (len > 0 && len < _countof(drives_buf))
    {
        for (const wchar_t* p = drives_buf; *p != L'\0'; p += wcslen(p) + 1)
        {
            // p 形如 "C:\", 取 "C:"
            wchar_t      drive_letter = p[0];
            std::wstring device;
            if (!PathQueryDriveDevice(drive_letter, device))
                continue;

            size_t dev_len = device.size();
            if (nt_path.size() >= dev_len &&
                _wcsnicmp(nt_path.c_str(), device.c_str(), dev_len) == 0 &&
                (nt_path.size() == dev_len || nt_path[dev_len] == L'\\'))
            {
                wchar_t drive[3] = { drive_letter, L':', L'\0' };
                return std::wstring(drive) + nt_path.substr(dev_len);
            }
        }
    }

    // 无法转换，原样返回
    return nt_path;
}
