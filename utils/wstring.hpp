#ifndef APPBOX_UTILS_WSTRING_HPP
#define APPBOX_UTILS_WSTRING_HPP

#include <string>

namespace appbox
{

/**
 * Converts a multibyte string to a wide string using the specified code page.
 *
 * This function takes a multibyte string and a code page and converts
 * the string into a wide string format. It calculates the size needed
 * for the wide string, then performs the conversion.
 *
 * @param[in] s The multibyte string to convert.
 * @param[in] codepage The code page used for the conversion. Defaults to CP_ACP if not provided.
 * @return A wide string representation of the input multibyte string.
 */
std::wstring mbstowcs(const char* s, int codepage = CP_ACP);

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
 * @param[in] n The maximum number of characters to process from the multibyte string.
 * @param[in] codepage The code page used for the conversion. Defaults to CP_ACP if not provided.
 * @return A wide string representation of the specified portion of the input multibyte string.
 */
std::wstring mbstowcs(const char* s, size_t n, int codepage = CP_ACP);

/**
 * Converts a wide string to a multibyte string using the specified code page.
 *
 * This function takes a wide string and a code page and converts
 * the string into a multibyte string format. It internally calculates the size
 * required for the multibyte string and performs the conversion.
 *
 * @param[in] s The wide string to convert.
 * @param[in] codepage The code page used for the conversion. Defaults to CP_UTF8 if not explicitly
 * provided.
 * @return A multibyte string representation of the input wide string.
 */
std::string wcstombs(const wchar_t* s, int codepage = CP_UTF8);

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
 * @param[in] n The maximum number of characters to process from the wide string.
 * @param[in] codepage The code page used for the conversion. Defaults to CP_UTF8 if not explicitly
 * provided.
 * @return A multibyte string representation of the specified portion of the input wide string.
 */
std::string wcstombs(const wchar_t* s, size_t n, int codepage = CP_UTF8);

}

#endif
