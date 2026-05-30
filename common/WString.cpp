#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <stdexcept>
#include "WString.hpp"

std::wstring appbox::UTF8ToWide(const char* s)
{
    int size_need = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (size_need < 0)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }
    wchar_t* new_str = new wchar_t[size_need];
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, new_str, size_need) != size_need)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }
    std::wstring dst(new_str);
    delete[] new_str;
    return dst;
}

std::wstring appbox::UTF8ToWide(const std::string& s)
{
    return UTF8ToWide(s.c_str());
}

std::string appbox::WideToUTF8(const wchar_t* s)
{
    int   size_need = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    char* new_str = new char[size_need];
    WideCharToMultiByte(CP_UTF8, 0, s, -1, new_str, size_need, nullptr, nullptr);
    std::string dst(new_str);
    delete[] new_str;
    return dst;
}

std::string appbox::WideToUTF8(const std::wstring& s)
{
    return WideToUTF8(s.c_str());
}

std::vector<std::wstring> appbox::Split(const std::wstring& str, const std::wstring& p)
{
    std::vector<std::wstring> result;

    if (p.empty())
    {
        result.push_back(str);
        return result;
    }

    std::wstring::size_type start = 0;
    std::wstring::size_type pos = 0;

    while ((pos = str.find(p, start)) != std::wstring::npos)
    {
        result.emplace_back(str.substr(start, pos - start));
        start = pos + p.size();
    }

    /* Include the last part */
    result.emplace_back(str.substr(start));

    return result;
}

bool appbox::PrefixCompareExchange(const std::wstring& str, const std::wstring& pat, const std::wstring& rep,
                                   bool ignore_case, std::wstring& out)
{
    // 前缀长度不可能比原串还长
    if (pat.size() > str.size())
    {
        return false;
    }

    // 空前缀视为总是匹配，跳过 API 调用（CompareStringOrdinal 对长度 0 行为良好，
    // 但显式处理可读性更好，也可避免对空 wstring 调用 data() 的潜在问题）
    if (!pat.empty())
    {
        const int cch = static_cast<int>(pat.size());
        const int cmp = ::CompareStringOrdinal(str.data(), cch, pat.data(), cch, ignore_case ? TRUE : FALSE);

        if (cmp != CSTR_EQUAL)
        {
            // 比较失败或不相等
            return false;
        }
    }

    // 构造替换结果。先组装到临时变量，再移动到 ret，
    // 这样可以正确处理 ret 与 str/rep/pat 是同一对象（别名）的情况，
    // 同时保证“仅在替换成功时才赋值”的语义。
    std::wstring tmp;
    tmp.reserve(rep.size() + (str.size() - pat.size()));
    tmp.append(rep);
    tmp.append(str, pat.size(), std::wstring::npos);

    out = std::move(tmp);
    return true;
}
