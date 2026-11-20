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

/**
 * @brief Encode a wide text into the code page of the machine.
 *
 * The ANSI entry points of the environment read their arguments in the code
 * page of the machine, and the texts of the cases and of the probes travel in
 * UTF-8, so the argument of such an entry point is encoded back before the
 * call. A character the code page has no bytes for is replaced by the default
 * character of the system.
 *
 * @param[in] text Wide text to convert.
 * @return The text of the argument, spelled in the code page of the machine,
 *         empty when it cannot be converted.
 */
std::string WideToAcp(const std::wstring& text);

/**
 * @brief Decode the text of the code page of the machine into UTF-8.
 *
 * The ANSI view of the environment of a process is the view of the block of
 * the process through the code page of the machine: the bytes it reports are
 * spelled in the code page, and the payload of the session carries UTF-8 only,
 * so the view is decoded before it enters the answer. Bytes which the code
 * page does not spell are replaced by the system, and a text the code page
 * cannot answer at all is answered as an empty one.
 *
 * @param[in] text Text spelled in the code page of the machine.
 * @return The UTF-8 representation, empty when the text cannot be converted.
 */
std::string AcpToUTF8(const std::string& text);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TESTTEXT_HPP
