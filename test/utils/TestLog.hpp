#ifndef APPBOX_TEST_UTILS_TESTLOG_HPP
#define APPBOX_TEST_UTILS_TESTLOG_HPP

#include <cstddef>

namespace appbox::test
{

/**
 * @brief Number of messages the backtrace of the test run keeps.
 *
 * The buffer is a ring: it holds the newest messages of the running case, so
 * the log next to a failure survives even when a case produces more messages
 * than the buffer holds. One entry copies the formatted message, so the value
 * is a memory budget as well.
 */
inline constexpr std::size_t kBacktraceMessages = 1024;

/**
 * @brief Silence the program output of the test run and capture it.
 *
 * The default logger of the process drops its messages into a `spdlog`
 * backtrace instead of the console, and the backtrace is dumped when a test
 * case fails. GoogleTest writes to the stream itself, so its lines and the
 * assertions of a case are printed while the run works as before.
 *
 * The call belongs after `CLI11_PARSE` and before `RUN_ALL_TESTS`.
 */
void InstallTestLog();

/**
 * @brief Write the captured program output of the running test case.
 *
 * A case which failed is dumped by the listener of `InstallTestLog`. This
 * entry point exists for the watchdog of a timed out case, which takes the run
 * down without a GoogleTest `OnTestEnd` event.
 */
void DumpTestLog();

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TESTLOG_HPP
