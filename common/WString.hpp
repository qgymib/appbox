#ifndef APPBOX_COMMON_WSTRING_HPP
#define APPBOX_COMMON_WSTRING_HPP

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
 * @return A wide string representation of the specified portion of the input multibyte string.
 */
std::wstring UTF8ToWide(const char* s);

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
std::string WideToUTF8(const wchar_t* s);

} // namespace appbox

#endif // APPBOX_COMMON_WSTRING_HPP
