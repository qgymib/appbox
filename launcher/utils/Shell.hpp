#ifndef APPBOX_LAUNCHER_UTILS_SHELL_HPP
#define APPBOX_LAUNCHER_UTILS_SHELL_HPP

#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Resolve the shell of the host from two values of the environment.
 *
 * The shell which runs inside the sandbox is the `cmd.exe` of the machine which
 * runs the sandbox, not a program the archive carries: the launcher resolves the
 * path outside of the isolation, so the sandboxed process starts the real file
 * through the host layer of its filesystem view.
 *
 * @param[in] comspec Value of `%COMSPEC%`, empty when the variable is not set.
 * @param[in] system_root Value of `%SystemRoot%`, empty when the variable is
 *                        not set.
 * @return Path of the shell, empty when neither value names an existing file.
 */
std::wstring ResolveShellPath(const std::wstring& comspec, const std::wstring& system_root);

/**
 * @brief Resolve the shell of the host from the environment of this process.
 * @return Path of the shell, empty when it cannot be resolved.
 */
std::wstring ResolveShellPath();

/**
 * @brief Build the arguments which run one command through the shell.
 *
 * Without a command the shell is started without any argument, which makes it
 * run interactively. With a command the arguments are `/c` followed by the
 * command, so the shell runs the command and leaves afterwards; the caller
 * spells the command itself out and does not pass `/c` of its own.
 *
 * The command is handed over as separate arguments and quoted by
 * `appbox::BuildCommandLine()`: `cmd.exe` joins everything behind `/c` back
 * into one command line, so an argument which holds a space survives. A
 * command whose first token has to be quoted needs the extra quote rule of
 * `cmd.exe` and is not supported.
 *
 * @param[in] params Command of the shell, in UTF-8; empty to run the shell
 *                   itself.
 * @return Empty for an interactive shell, otherwise `/c` followed by the
 *         parameters.
 */
std::vector<std::wstring> BuildShellArguments(const std::vector<std::string>& params);

} // namespace appbox

#endif
