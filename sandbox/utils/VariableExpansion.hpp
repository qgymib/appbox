#ifndef APPBOX_SANDBOX_UTILS_VARIABLE_EXPANSION_HPP
#define APPBOX_SANDBOX_UTILS_VARIABLE_EXPANSION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
/*
 * The path is spelled relative to this header, because the test executable
 * compiles the file as well and its include path holds the configuration of the
 * launcher under the very same name.
 */
#include "../Config.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief One variable the sandbox expands in a value of the workspace.
 *
 * The values the user enters in the Environment and the Registry workspace may
 * reference a known folder of the machine which runs the sandbox instead of
 * spelling its path out. The syntax of a reference is `%APPBOX:<NAME>%`, where
 * the name is the layer key of a known folder without its `#` delimiters
 * (`#ProgramFiles#` names the variable `ProgramFiles`).
 *
 * The launcher resolves the paths of the known folders of the machine and hands
 * the list to the sandbox, so the expansion never depends on the machine which
 * packed the archive: the archive keeps the references and the sandbox
 * replaces them while it runs.
 */
struct VariableMapping
{
    /**
     * @brief Name of the variable without the `%APPBOX:` prefix.
     *
     * The name of a reference is compared with this name by code unit, so the
     * case matters: `ProgramFiles` and `ProgramFilesX64` are the layer keys the
     * packer writes, and `programfiles` names no variable at all.
     */
    std::wstring name;

    /**
     * @brief Text a reference to the variable is replaced with.
     */
    std::wstring path;
};

/**
 * @brief Prefix which introduces a reference of the sandbox.
 *
 * A `%NAME%` reference belongs to the shell and is left alone: only a
 * reference whose name starts with this prefix names a variable of the
 * sandbox, so a value which carries `%PATH%` keeps it for the expansion of the
 * operating system.
 *
 * The prefix is compared by code unit, so it is spelled `APPBOX:` in upper
 * case; another spelling (`appbox:`) names no variable of the sandbox and keeps
 * the reference as it is.
 */
inline constexpr std::wstring_view kVariablePrefix = L"APPBOX:";

/**
 * @brief Select the variables of one process out of the injected list.
 *
 * The launcher resolves every known folder twice, because the operating system
 * shows a different folder to a 32 bit process than to a 64 bit one:
 * `%ProgramFiles%` names `C:\Program Files` in a 64 bit process and
 * `C:\Program Files (x86)` in a 32 bit one. The sandbox picks the view which
 * matches the bitness of the process it is injected into, so a 32 bit
 * application which starts a 64 bit program gives the child the paths of a 64
 * bit process while the application itself keeps its own.
 *
 * A variable whose view is empty is left out of the table: its folder does not
 * exist for the process, which is the `Not applicable` row of the table of the
 * expansion. A reference to such a name keeps its own spelling, like a
 * reference to a name the sandbox does not know.
 *
 * @param[in] variables Variables of the injected configuration.
 * @param[in] is_64_bit Whether the process which expands the values is 64 bit.
 * @return The variables of the process, in the order of the configuration.
 */
std::vector<VariableMapping> SelectVariables(const std::vector<SandboxVariable>& variables, bool is_64_bit);

/**
 * @brief Expand every reference of a text.
 *
 * The scan runs from left to right and an expansion is never resolved again,
 * so the text a reference was replaced with is copied as it is. A reference
 * whose name is not listed, a lone `%`, a reference without a closing `%` and
 * a reference without the prefix keep their own spelling, which is what the
 * operating system does with a `%NAME%` reference it cannot resolve.
 *
 * The prefix and the name are compared by code unit, so the case of a reference
 * has to match the table: `%APPBOX:ProgramFiles%` is expanded, while
 * `%appbox:ProgramFiles%` and `%APPBOX:programfiles%` keep their spelling like
 * a name the table does not list. Both are reported in the log, because a
 * mistyped reference is otherwise hard to find in an archive.
 *
 * The function is free of the sandbox singleton: the caller passes the table,
 * so the rules are unit testable on their own.
 *
 * @param[in] text Text which may carry references.
 * @param[in] variables Variables the sandbox knows.
 * @return The text with every known reference replaced.
 */
std::wstring ExpandVariables(std::wstring_view text, const std::vector<VariableMapping>& variables);

/**
 * @brief Expand the references of a registry value of a string type.
 *
 * `REG_SZ` and `REG_EXPAND_SZ` hold one UTF-16 string, `REG_MULTI_SZ` holds a
 * list of them whose items are terminated by a null character. Every item of
 * the list is expanded on its own and the separators are kept exactly as they
 * are, so the shape of the value does not change.
 *
 * Every other type, a data blob which is not made of whole wide characters and
 * a value without a reference are returned unchanged, so the caller can tell
 * whether a write back is needed by comparing the result with the data it
 * passed.
 *
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @param[in] variables Variables the sandbox knows.
 * @return The data of the value with every known reference replaced.
 */
std::vector<BYTE> ExpandRegistryValueData(DWORD type, const std::vector<BYTE>& data,
                                          const std::vector<VariableMapping>& variables);

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_VARIABLE_EXPANSION_HPP
