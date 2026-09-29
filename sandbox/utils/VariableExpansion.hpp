#ifndef APPBOX_SANDBOX_UTILS_VARIABLE_EXPANSION_HPP
#define APPBOX_SANDBOX_UTILS_VARIABLE_EXPANSION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
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
 * the name is the layer key of a preset directory of the packer without its
 * `#` delimiters (`#ProgramFiles#` names the variable `ProgramFiles`).
 *
 * The loader resolves the paths of the known folders of the machine and hands
 * the list to the sandbox, so the expansion never depends on the machine which
 * packed the archive: the archive keeps the references and the sandbox
 * replaces them while it runs.
 */
struct VariableMapping
{
    /**
     * @brief Name of the variable without the `%APPBOX:` prefix.
     *
     * The name of a reference is compared with this name ignoring the case,
     * because the layer keys of the packer are spelled in different cases
     * (`ProgramFiles` next to `USERPROFILE`).
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
 */
inline constexpr std::wstring_view kVariablePrefix = L"APPBOX:";

/**
 * @brief Expand every reference of a text.
 *
 * The scan runs from left to right and an expansion is never resolved again,
 * so the text a reference was replaced with is copied as it is. A reference
 * whose name is not listed, a lone `%`, a reference without a closing `%` and
 * a reference without the prefix keep their own spelling, which is what the
 * operating system does with a `%NAME%` reference it cannot resolve.
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
