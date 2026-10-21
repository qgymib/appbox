#ifndef APPBOX_SANDBOX_ENVIRONMENT_ISOLATION_HPP
#define APPBOX_SANDBOX_ENVIRONMENT_ISOLATION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
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
 * and installs the composition as the environment block of the process. Every
 * reader therefore sees the view of the sandbox: an entry point of the
 * environment API, the loader of the operating system and a module which walks
 * the block of the process parameters itself alike. The environment of the host
 * is never modified, because the block of the host is replaced before the first
 * line of the application runs, and no reader of the environment is hooked.
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
 * Only the lowest writer of the environment is hooked: a modification the
 * application makes is written into the block of the process by the operating
 * system, which keeps every reader consistent, and is recorded for the state
 * file at the same time. The state travels to the launcher through the RPC
 * method of the environment, so it survives the end of the process which made
 * it.
 *
 * A missing or malformed document is never fatal: the module logs the reason
 * and keeps the environment it composed so far, which is the behaviour of a
 * sandbox without that document.
 */
class Isolation
{
public:
    /**
     * @brief Compose the environment of the sandbox and install it.
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
     * @brief Record a variable the application stored.
     *
     * The block of the process is written by the operating system before this
     * runs; the call only keeps the record of the sandbox in step, which is
     * what the next run reads.
     *
     * @param[in] name Name of the variable.
     * @param[in] value Value of the variable.
     * @return true when the modification was recorded.
     */
    static bool Store(const std::wstring& name, const std::wstring& value);

    /**
     * @brief Record a variable the application removed.
     *
     * Removing a variable which is not listed succeeds, because the result the
     * caller asked for — the variable is not part of the environment — holds.
     *
     * @param[in] name Name of the variable.
     * @return true when the modification was recorded.
     */
    static bool Remove(const std::wstring& name);

    /**
     * @brief Record a whole block the application installed.
     *
     * The block is the new environment of the process, so the table of the
     * sandbox is replaced by it. What the state records is the difference
     * between the block and the environment the sandbox composed: a variable the
     * block carries with another value or with no value at all is recorded, and
     * a variable the block dropped is recorded as a removal. A replacement which
     * carries the environment of the process therefore records nothing, which is
     * what keeps the environment of the host out of the state.
     *
     * @param[in] block The block to read, may be null for an empty environment.
     * @param[in] size_in_bytes Length of the block in bytes, zero for a block
     *                          which ends with a double null character.
     */
    static void AssignBlock(const wchar_t* block, std::size_t size_in_bytes = 0);

    /**
     * @brief Record that the application dropped every variable.
     */
    static void Clear();
};

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
 * @brief Convert text of the ANSI code page into UTF-16.
 * @param[in] text The text to convert.
 * @param[out] out The converted text.
 * @return true on success.
 */
bool AnsiToWide(const std::string& text, std::wstring& out);

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
 * @brief Whether a writer names the environment of the process.
 *
 * `RtlSetEnvironmentVariable` takes the address of the block it writes. A null
 * address names the environment of the process, and so does the address of the
 * environment pointer of the process parameters: the entry point of `ntdll`
 * compares the block it is given against that pointer and takes the lock of the
 * process block when the two match, which is what makes the two spellings the
 * same call.
 *
 * @param[in] environment The block the caller named, may be null.
 * @return true when the call writes the environment of the process.
 */
bool IsProcessEnvironment(PWSTR* environment);

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

} // namespace environment
} // namespace appbox

#endif // APPBOX_SANDBOX_ENVIRONMENT_ISOLATION_HPP
