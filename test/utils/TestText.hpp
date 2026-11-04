#ifndef APPBOX_TEST_UTILS_TESTTEXT_HPP
#define APPBOX_TEST_UTILS_TESTTEXT_HPP

#include <string>

namespace appbox::test
{

/**
 * @brief Convert a wide text to UTF-8 for a case or for a probe which reports it.
 *
 * A probe runs inside the sandbox and reports the names it observed through a
 * protocol structure, and a case reports the names it produced with the same
 * conversion. `WideToUTF8()` answers the failure of a conversion by a code, and
 * the report of a probe or a case is written by hand and never fails, so the
 * helper hands back an empty text for a text the code page cannot carry, which
 * is the wording the tests have always used for a name they cannot read.
 *
 * @param[in] text Wide text to convert.
 * @return The UTF-8 representation, empty when the text cannot be converted.
 */
std::string ToUTF8OrEmpty(const std::wstring& text);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TESTTEXT_HPP
