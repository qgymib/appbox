#include <cwctype>
#include <vector>
#include "MappingAsSandboxNtPath.hpp"

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

static bool IsSlash(wchar_t ch)
{
    return ch == L'\\' || ch == L'/';
}

static bool IsDriveLetter(wchar_t ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

// 只解析 \??\D:\xxx 或 \GLOBAL??\D:\xxx
static bool ParseNtDosDrivePath(const std::wstring& path, wchar_t& drive, std::wstring& rest)
{
    size_t prefixLen = 0;

    if (StartsWithI(path, L"\\??\\"))
    {
        prefixLen = 4;
    }
    else if (StartsWithI(path, L"\\GLOBAL??\\"))
    {
        prefixLen = 10;
    }
    else
    {
        return false;
    }

    // 必须是 X:\...
    if (path.size() < prefixLen + 3)
        return false;

    wchar_t d = path[prefixLen];
    if (!IsDriveLetter(d) || path[prefixLen + 1] != L':' || !IsSlash(path[prefixLen + 2]))
    {
        return false;
    }

    drive = std::towupper(d);
    rest = path.substr(prefixLen + 3); // 不含 "D:\"

    return true;
}

// 归一化 a\b\..\c => a\c；若试图越过根则失败
static bool NormalizeRelativePath(const std::wstring& in, std::wstring& out)
{
    std::vector<std::wstring> parts;
    size_t                    i = 0;

    while (i < in.size())
    {
        while (i < in.size() && IsSlash(in[i]))
            ++i;

        size_t start = i;
        while (i < in.size() && !IsSlash(in[i]))
            ++i;

        if (start == i)
            break;

        std::wstring part = in.substr(start, i - start);

        if (part == L"." || part.empty())
        {
            continue;
        }

        if (part == L"..")
        {
            if (parts.empty())
                return false; // 防止越过 D:\ 根
            parts.pop_back();
            continue;
        }

        parts.push_back(std::move(part));
    }

    out.clear();
    for (size_t n = 0; n < parts.size(); ++n)
    {
        if (n != 0)
            out += L'\\';
        out += parts[n];
    }

    return true;
}

static void TrimTrailingSlashes(std::wstring& s)
{
    while (s.size() > 1 && IsSlash(s.back()))
        s.pop_back();
}

bool appbox::MappingPathInSandbox(const std::wstring& path, const std::wstring& sandbox, std::wstring& out)
{
    wchar_t      drive = 0;
    std::wstring rest;

    if (!ParseNtDosDrivePath(path, drive, rest))
        return false;

    // sandbox 本身也必须是本地 DOS 盘符 NT 路径
    wchar_t      sandboxDrive = 0;
    std::wstring sandboxRest;
    if (!ParseNtDosDrivePath(sandbox, sandboxDrive, sandboxRest))
        return false;

    std::wstring normalizedRest;
    if (!NormalizeRelativePath(rest, normalizedRest))
        return false;

    std::wstring root = sandbox;
    TrimTrailingSlashes(root);

    out = root;
    out += L'\\';
    out += drive;

    if (!normalizedRest.empty())
    {
        out += L'\\';
        out += normalizedRest;
    }

    return true;
}
