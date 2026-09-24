#ifndef APPBOX_TEST_UTILS_COMMANDLINE_HPP
#define APPBOX_TEST_UTILS_COMMANDLINE_HPP

#include <string>

namespace appbox::test
{

/**
 * @brief Read the command line of the current process.
 *
 * The wide command line is read instead of the `argv` of `main()`, so an
 * argument which holds characters outside of the ANSI code page (a user name
 * with non-ASCII characters in a path, for example) survives.
 *
 * The helpers of this module work before GoogleTest and CLI11 are initialized,
 * which is what the coredump writer of a timed out test needs.
 *
 * @return The command line of the current process.
 */
std::wstring GetOwnCommandLine();

/**
 * @brief Find the value of a `--name=value` option in a command line.
 *
 * The name is matched as a whole token: an option has to start the command
 * line or follow a space, a tab or the quote of a quoted token. A value which
 * the builder of the command line quoted is returned without the quotes.
 *
 * @param[in] command_line The command line to search.
 * @param[in] name The option name without the leading `--` and without `=`.
 * @param[out] value The value of the option; cleared when it is not there.
 * @return true when the option is part of the command line.
 */
bool FindCommandLineOption(const std::wstring& command_line, const std::wstring& name, std::wstring& value);

/**
 * @brief Read an environment variable of the current process.
 *
 * An empty variable counts as set, which is how a caller distinguishes "not
 * set" from "set to the empty string".
 *
 * @param[in] name Name of the variable.
 * @param[out] value Value of the variable.
 * @return true when the variable is set.
 */
bool ReadEnvironmentVariable(const std::wstring& name, std::wstring& value);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_COMMANDLINE_HPP
