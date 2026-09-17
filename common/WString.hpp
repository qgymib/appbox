#ifndef APPBOX_COMMON_WSTRING_HPP
#define APPBOX_COMMON_WSTRING_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
/*
 * Do not let winnt.h define the STATUS_* constants: consumers which also
 * include ntstatus.h would get macro redefinitions, which are errors under
 * the project wide /W4 /WX settings.
 */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Converts a multibyte string to a wide string using UTF-8.
 *
 * This function calculates the necessary size for the wide string and then
 * performs the conversion.
 *
 * @param[in] s The multibyte string to convert.
 * @return A wide string representation of the input multibyte string.
 * @throw std::runtime_error The input is null or the conversion failed.
 */
inline std::wstring UTF8ToWide(const char* s)
{
    if (s == nullptr)
    {
        throw std::runtime_error("UTF8ToWide() failed: input is null");
    }

    /* Both conversion functions return 0 on failure, never a negative value. */
    int size_need = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (size_need <= 0)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }

    auto new_str = std::make_unique<wchar_t[]>(static_cast<size_t>(size_need));
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, new_str.get(), size_need) != size_need)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }

    return std::wstring(new_str.get());
}

/**
 * @brief Converts a multibyte string to a wide string using UTF-8.
 * @param[in] s The multibyte string to convert.
 * @return A wide string representation of the input multibyte string.
 * @throw std::runtime_error The conversion failed.
 */
inline std::wstring UTF8ToWide(const std::string& s)
{
    return UTF8ToWide(s.c_str());
}

/**
 * @brief Converts a wide string to a multibyte string using UTF-8.
 *
 * This function calculates the necessary size for the multibyte string and
 * then performs the conversion.
 *
 * @param[in] s The wide string to convert.
 * @return A multibyte string representation of the input wide string.
 * @throw std::runtime_error The input is null or the conversion failed.
 */
inline std::string WideToUTF8(const wchar_t* s)
{
    if (s == nullptr)
    {
        throw std::runtime_error("WideToUTF8() failed: input is null");
    }

    int size_need = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (size_need <= 0)
    {
        throw std::runtime_error("WideCharToMultiByte() failed");
    }

    auto new_str = std::make_unique<char[]>(static_cast<size_t>(size_need));
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, new_str.get(), size_need, nullptr, nullptr) != size_need)
    {
        throw std::runtime_error("WideCharToMultiByte() failed");
    }

    return std::string(new_str.get());
}

/**
 * @brief Converts a wide string to a multibyte string using UTF-8.
 * @param[in] s The wide string to convert.
 * @return A multibyte string representation of the input wide string.
 * @throw std::runtime_error The conversion failed.
 */
inline std::string WideToUTF8(const std::wstring& s)
{
    return WideToUTF8(s.c_str());
}

/**
 * @brief Split string.
 * @param[in] str The string to split.
 * @param[in] p The delimiter to split the string.
 * @return The vector of split strings.
 */
inline std::vector<std::wstring> Split(const std::wstring& str, const std::wstring& p)
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

/**
 * @brief Prefix compare and exchange.
 * @param[in] str The string to compare.
 * @param[in] pat The prefix string to compare.
 * @param[in] rep The replacement string.
 * @param[in] ignore_case Whether to ignore case when comparing.
 * @param[out] out The output string.
 * @return Whether the prefix is matched.
 */
inline bool PrefixCompareExchange(const std::wstring& str, const std::wstring& pat, const std::wstring& rep,
                                  bool ignore_case, std::wstring& out)
{
    /* The prefix can not be longer than the string itself. */
    if (pat.size() > str.size())
    {
        return false;
    }

    /* An empty prefix always matches, skip the API call in that case. */
    if (!pat.empty())
    {
        const int cch = static_cast<int>(pat.size());
        const int cmp = ::CompareStringOrdinal(str.data(), cch, pat.data(), cch, ignore_case ? TRUE : FALSE);

        if (cmp != CSTR_EQUAL)
        {
            /* The comparison failed or the prefixes are not equal. */
            return false;
        }
    }

    /*
     * Build the replacement into a temporary first, then move it to the
     * output. This handles the case that out aliases str, rep or pat, and
     * keeps the "assign only on success" semantics.
     */
    std::wstring tmp;
    tmp.reserve(rep.size() + (str.size() - pat.size()));
    tmp.append(rep);
    tmp.append(str, pat.size(), std::wstring::npos);

    out = std::move(tmp);
    return true;
}

} // namespace appbox

#endif // APPBOX_COMMON_WSTRING_HPP
