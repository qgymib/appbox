#ifndef APPBOX_SANDBOX_ENVIRONMENT_ISOLATION_HPP
#define APPBOX_SANDBOX_ENVIRONMENT_ISOLATION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include <cstddef>
#include <string>

namespace appbox
{
namespace environment
{

/**
 * @brief The environment isolation of the sandbox.
 *
 * The module composes the environment of the packaged application while the
 * sandbox DLL is injected (see `common/EnvironmentIsolation.hpp` for the rules)
 * and keeps every modification the application makes to it. The environment
 * block of the process itself is never touched: the entry points which read,
 * write, enumerate or expand a variable of the process environment are hooked
 * and answered from the private table of the sandbox, so the environment of the
 * host is never modified, whatever the application does.
 *
 * The composed environment is built from three sources, in this order:
 *
 * 1. The environment of the host, which is the block of this process: the
 *    module is initialized before the hooks are attached, so the block is read
 *    through the original entry points.
 * 2. The variables of the environment isolation file of the archive, composed
 *    with the values of the host.
 * 3. The modifications of the state file of the sandbox, which an earlier run
 *    of the application made and which win over the two sources above.
 *
 * Every modification the application makes afterwards is written back to the
 * state file through the RPC method of the launcher, so it survives the end of
 * the process which made it.
 *
 * A missing or malformed document is never fatal: the module logs the reason
 * and keeps the environment it composed so far, which is the behaviour of a
 * sandbox without that document.
 */
class Isolation
{
public:
    /**
     * @brief Compose the environment of the sandbox.
     *
     * The module has to be initialized before the hooks are attached, so the
     * environment of the host and the documents are read through the original
     * entry points of the process.
     *
     * @return Status code.
     */
    static NTSTATUS Init();

    /**
     * @brief Drop the environment of the sandbox.
     */
    static void Exit();

    /**
     * @brief Whether the sandbox keeps an environment of its own.
     * @return true while the process runs inside the sandbox.
     */
    static bool IsEnabled();

    /**
     * @brief Number of variables of the sandbox environment.
     * @return The number of variables.
     */
    static std::size_t Count();

    /**
     * @brief Whether the sandbox environment lists a variable.
     * @param[in] name Name to look for, compared ignoring the case.
     * @return true when the variable is listed.
     */
    static bool Contains(const std::wstring& name);

    /**
     * @brief Read a variable of the sandbox environment.
     * @param[in] name Name to look for, compared ignoring the case.
     * @param[out] value Value of the variable, untouched when it is not listed.
     * @return true when the variable is listed.
     */
    static bool Query(const std::wstring& name, std::wstring& value);

    /**
     * @brief Store a variable of the sandbox environment.
     *
     * The name is refused when it is empty or carries an equals sign, which is
     * what the environment of a process cannot hold.
     *
     * @param[in] name Name of the variable.
     * @param[in] value Value of the variable.
     * @return true when the variable was stored.
     */
    static bool Store(const std::wstring& name, const std::wstring& value);

    /**
     * @brief Remove a variable of the sandbox environment.
     *
     * Removing a variable which is not listed succeeds, because the result the
     * caller asked for — the variable is not part of the environment — holds.
     *
     * @param[in] name Name of the variable.
     * @return true when the name is usable.
     */
    static bool Remove(const std::wstring& name);

    /**
     * @brief Replace the whole sandbox environment from a block.
     * @param[in] block The block to read, may be null for an empty environment.
     */
    static void AssignBlock(const wchar_t* block);

    /**
     * @brief Drop every variable of the sandbox environment.
     */
    static void Clear();

    /**
     * @brief Build a block of the whole sandbox environment.
     * @return The block, which the caller releases with ReleaseBlock().
     */
    static wchar_t* CreateBlock();

    /**
     * @brief Build a block of the whole sandbox environment for a caller which
     *        owns it.
     *
     * The block is not tracked by the sandbox: a caller which creates an
     * environment of its own releases the block with the entry point which
     * destroys an environment, so the sandbox must not release it a second
     * time.
     *
     * @return The block, null when the allocation failed.
     */
    static wchar_t* CreateOwnedBlock();

    /**
     * @brief Release a block which CreateBlock() handed out.
     * @param[in] block The block to release.
     * @return true when the block belongs to the sandbox environment.
     */
    static bool ReleaseBlock(wchar_t* block);

    /**
     * @brief Build an ANSI block of the whole sandbox environment.
     * @return The block, which the caller releases with ReleaseAnsiBlock().
     */
    static char* CreateAnsiBlock();

    /**
     * @brief Release a block which CreateAnsiBlock() handed out.
     * @param[in] block The block to release.
     * @return true when the block belongs to the sandbox environment.
     */
    static bool ReleaseAnsiBlock(char* block);

    /**
     * @brief Expand the `%NAME%` references of a text.
     *
     * A reference the sandbox environment does not hold keeps its spelling,
     * which is what the operating system does, and a lone percent sign is
     * copied.
     *
     * @param[in] text The text to expand.
     * @return The expanded text.
     */
    static std::wstring Expand(const std::wstring& text);
};

/**
 * @brief Build the injected configuration of a child process.
 *
 * A process a sandboxed application starts inherits the environment of its
 * parent, which is the view of the sandbox already, so the configuration of the
 * child is the one of this process with the flag which tells the sandbox that
 * the environment of the child must not be composed a second time.
 *
 * @return The JSON document of the injected configuration, which is the one of
 *         this process while it cannot be adjusted.
 */
std::string BuildChildInjectData();

/**
 * @brief Resolve an entry point of the environment API of the process.
 *
 * The entry points live in `kernelbase.dll` on a current system and in
 * `kernel32.dll` on an older one, so both modules are asked. The load function
 * of a hook calls this while the hooks are attached.
 *
 * @param[in] name Name of the export.
 * @return The address of the entry point, null when neither module exports it.
 */
FARPROC ResolveEnvironmentProc(const char* name);

/**
 * @brief Read the text of a counted unicode string.
 *
 * The buffer of an UNICODE_STRING is not required to be null terminated and a
 * caller can pass a length which does not fit into the buffer it owns. Such a
 * structure is rejected instead of being read, because a hook must never read
 * outside the memory of the caller.
 *
 * @param[in] text The string to read, may be null.
 * @param[out] out The text of the string, empty while the string is not usable.
 * @return true when the string is usable.
 */
bool ReadUnicodeStringText(const PUNICODE_STRING text, std::wstring& out);

/**
 * @brief Convert text of the ANSI code page into UTF-16.
 *
 * @param[in] text The text to convert.
 * @param[out] out The converted text.
 * @return true on success.
 */
bool AnsiToWide(const std::string& text, std::wstring& out);

/**
 * @brief Convert UTF-16 text into the ANSI code page.
 *
 * A character the code page cannot express is written as the default character
 * of the code page, which is what the ANSI entry points of the operating
 * system do.
 *
 * @param[in] text The text to convert.
 * @param[out] out The converted text.
 * @return true on success.
 */
bool WideToAnsi(const std::wstring& text, std::string& out);

} // namespace environment
} // namespace appbox

#endif // APPBOX_SANDBOX_ENVIRONMENT_ISOLATION_HPP
