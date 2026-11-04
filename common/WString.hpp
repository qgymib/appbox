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
#include <string>
#include <tl/expected.hpp>
#include <vector>
#include "ErrorCode.hpp"

namespace appbox
{

/**
 * @brief Converts a multibyte string to a wide string using UTF-8.
 *
 * This function calculates the necessary size for the wide string and then
 * performs the conversion. A failure is reported by its code and never by an
 * exception, so the call is safe inside an interface which promises not to
 * throw.
 *
 * The conversion is strict: a byte sequence which is not UTF-8 - an unpaired
 * surrogate, a truncated sequence - is refused instead of being replaced by a
 * placeholder, so a name a caller cannot read reaches it as a code rather than
 * as a different name.
 *
 * @param[in] s The multibyte string to convert.
 * @return The wide text, or the class of the refusal:
 *         - `kTextInputNull` for a null pointer;
 *         - `kTextConversionFailed` for a text the code page cannot carry.
 */
inline tl::expected<std::wstring, ErrorCode> UTF8ToWide(const char* s) noexcept
{
    if (s == nullptr)
    {
        return tl::make_unexpected(ErrorCode::kTextInputNull);
    }

    /* Both conversion functions return 0 on failure, never a negative value. */
    const int size_need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (size_need <= 0)
    {
        return tl::make_unexpected(ErrorCode::kTextConversionFailed);
    }

    auto new_str = std::make_unique<wchar_t[]>(static_cast<size_t>(size_need));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, new_str.get(), size_need) != size_need)
    {
        return tl::make_unexpected(ErrorCode::kTextConversionFailed);
    }

    return std::wstring(new_str.get());
}

/**
 * @brief Converts a multibyte string to a wide string using UTF-8.
 * @param[in] s The multibyte string to convert.
 * @return The wide text, or the code of the failed conversion.
 */
inline tl::expected<std::wstring, ErrorCode> UTF8ToWide(const std::string& s) noexcept
{
    return UTF8ToWide(s.c_str());
}

/**
 * @brief Converts a wide string to a multibyte string using UTF-8.
 *
 * This function calculates the necessary size for the multibyte string and
 * then performs the conversion. A failure is reported by its code and never by
 * an exception, so the call is safe inside an interface which promises not to
 * throw.
 *
 * @param[in] s The wide string to convert.
 * @return The UTF-8 text, or the class of the refusal:
 *         - `kTextInputNull` for a null pointer;
 *         - `kTextConversionFailed` for a text the code page cannot carry.
 */
inline tl::expected<std::string, ErrorCode> WideToUTF8(const wchar_t* s) noexcept
{
    if (s == nullptr)
    {
        return tl::make_unexpected(ErrorCode::kTextInputNull);
    }

    int size_need = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (size_need <= 0)
    {
        return tl::make_unexpected(ErrorCode::kTextConversionFailed);
    }

    auto new_str = std::make_unique<char[]>(static_cast<size_t>(size_need));
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, new_str.get(), size_need, nullptr, nullptr) != size_need)
    {
        return tl::make_unexpected(ErrorCode::kTextConversionFailed);
    }

    return std::string(new_str.get());
}

/**
 * @brief Converts a wide string to a multibyte string using UTF-8.
 * @param[in] s The wide string to convert.
 * @return The UTF-8 text, or the code of the failed conversion.
 */
inline tl::expected<std::string, ErrorCode> WideToUTF8(const std::wstring& s) noexcept
{
    return WideToUTF8(s.c_str());
}

/**
 * @brief Split string.
 * @param[in] str The string to split.
 * @param[in] p The delimiter to split the string.
 * @return The vector of split strings.
 */
inline std::vector<std::wstring> Split(const std::wstring& str, const std::wstring& p) noexcept
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
 *
 * The prefix of `str` is compared with `pat` and, when the two are equal, it is
 * replaced by `rep`. A text which does not start with the prefix is reported by
 * the code `kPrefixMismatch` and never by an exception, so a caller which uses
 * the comparison as an ordinary branch reads the error side of the answer.
 *
 * @param[in] str The string to compare.
 * @param[in] pat The prefix string to compare.
 * @param[in] rep The replacement string.
 * @param[in] ignore_case Whether to ignore case when comparing.
 * @return The text with its prefix replaced, or `kPrefixMismatch` when `str`
 *         does not start with `pat`.
 */
inline tl::expected<std::wstring, ErrorCode> PrefixCompareExchange(const std::wstring& str, const std::wstring& pat,
                                                                   const std::wstring& rep, bool ignore_case) noexcept
{
    /* The prefix can not be longer than the string itself. */
    if (pat.size() > str.size())
    {
        return tl::make_unexpected(ErrorCode::kPrefixMismatch);
    }

    /* An empty prefix always matches, skip the API call in that case. */
    if (!pat.empty())
    {
        const int cch = static_cast<int>(pat.size());
        const int cmp = ::CompareStringOrdinal(str.data(), cch, pat.data(), cch, ignore_case ? TRUE : FALSE);

        if (cmp != CSTR_EQUAL)
        {
            /* The comparison failed or the prefixes are not equal. */
            return tl::make_unexpected(ErrorCode::kPrefixMismatch);
        }
    }

    std::wstring result;
    result.reserve(rep.size() + (str.size() - pat.size()));
    result.append(rep);
    result.append(str, pat.size(), std::wstring::npos);

    return result;
}

} // namespace appbox

#endif // APPBOX_COMMON_WSTRING_HPP
