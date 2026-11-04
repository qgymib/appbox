#ifndef APPBOX_TEST_UTILS_ERRORCODETEXT_HPP
#define APPBOX_TEST_UTILS_ERRORCODETEXT_HPP

#include "ErrorCode.hpp"
#include <string>

namespace appbox::test
{

/**
 * @brief Describe a code of `common/ErrorCode.hpp` for a test report.
 *
 * `common/` names a failure by its code alone, so a test which has to report
 * what an interface refused composes the text itself. The helper is the single
 * place of the test side which turns a code into text: a case which asserts a
 * code still prints a readable name when it fails, and the production modules
 * keep their own wording.
 *
 * @param[in] code Code an interface of `common/` reported.
 * @return The name of the code and its numeric value, for a message.
 */
std::string DescribeErrorCode(appbox::ErrorCode code);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_ERRORCODETEXT_HPP
