#ifndef APPBOX_UTILS_WSTRING_HPP
#define APPBOX_UTILS_WSTRING_HPP

#include "winapi.hpp"
#include <list>
#include <stdexcept>
#include <string>

namespace appbox
{

/**
 * Converts a multibyte string to a wide string using the specified code page,
 * with a limitation on the number of characters to process.
 *
 * This function takes a multibyte string, a size limit, and a code page to
 * convert the specified portion of the string into wide string format. It
 * calculates the necessary size for the wide string and then performs the
 * conversion based on the provided parameters.
 *
 * @param[in] s The multibyte string to convert.
 * @param[in] codepage The code page used for the conversion. Defaults to CP_UTF8 if not provided.
 * @return A wide string representation of the specified portion of the input multibyte string.
 */
inline std::wstring mbstowcs(const char* s, int codepage = CP_UTF8)
{
    int size_need = MultiByteToWideChar(codepage, 0, s, -1, nullptr, 0);
    if (size_need < 0)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }
    wchar_t* new_str = new wchar_t[size_need];
    if (MultiByteToWideChar(codepage, 0, s, -1, new_str, size_need) != size_need)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }
    std::wstring dst(new_str);
    delete[] new_str;
    return dst;
}

/**
 * Converts a wide string to a multibyte string using the specified code page,
 * with a limitation on the number of characters to process.
 *
 * This function takes a wide string, a size limit, and a code page to convert
 * the specified portion of the string into multibyte string format. It calculates
 * the necessary size for the multibyte string and performs the conversion based
 * on the provided parameters.
 *
 * @param[in] s The wide string to convert.
 * @param[in] codepage The code page used for the conversion. Defaults to CP_UTF8 if not explicitly
 * provided.
 * @return A multibyte string representation of the specified portion of the input wide string.
 */
inline std::string wcstombs(const wchar_t* s, int codepage = CP_UTF8)
{
    int   size_need = WideCharToMultiByte(codepage, 0, s, -1, nullptr, 0, nullptr, nullptr);
    char* new_str = new char[size_need];
    WideCharToMultiByte(codepage, 0, s, -1, new_str, size_need, nullptr, nullptr);
    std::string dst(new_str);
    delete[] new_str;
    return dst;
}

/**
 * @brief Splits the string \p str into substrings wherever \p sep occurs, and
 *   returns the list of those strings. If sep does not match anywhere in the
 *   string, it returns a single-element list containing this string.
 * @param[in] str The string to split.
 * @param[in] sep The string to match.
 * @param[in] keep_empty Whether empty entries should be kept.
 * @return The separated string list.
 */
inline std::list<std::wstring> StringSplit(const std::wstring& str, const std::wstring& sep,
                                           bool keep_empty)
{
    std::list<std::wstring> result;

    // If the separator is empty, return the whole string as a single element
    if (sep.empty())
    {
        result.push_back(str);
        return result;
    }

    std::wstring::size_type start = 0;
    std::wstring::size_type end = 0;

    while ((end = str.find(sep, start)) != std::wstring::npos)
    {
        // Extract substring between start and the separator
        if (end > start || keep_empty)
        {
            result.push_back(str.substr(start, end - start));
        }
        // Move start to the character after the separator
        start = end + sep.length();
    }

    // Add the remaining part of the string
    if (start < str.length() || keep_empty)
    {
        result.push_back(str.substr(start));
    }

    return result;
}

} // namespace appbox

#endif
