#ifndef APPBOX_PACKER_CORE_KNOWN_FOLDER_REFERENCE_HPP
#define APPBOX_PACKER_CORE_KNOWN_FOLDER_REFERENCE_HPP

#include "PresetDirectory.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Prefix of a reference to a known folder.
 *
 * The prefix is the one the sandbox expands, see
 * `sandbox/utils/VariableExpansion.hpp`: the packer writes the reference into
 * the archive and the sandbox replaces it with the path of the known folder of
 * the machine which runs the packaged application.
 */
inline constexpr const wchar_t* kKnownFolderReferencePrefix = L"%APPBOX:";

/**
 * @brief Build the name of the reference of a preset directory.
 *
 * The name is the layer key of the preset directory without its `#`
 * delimiters, so `#ProgramFiles#` names the reference `%APPBOX:ProgramFiles%`.
 *
 * @param[in] layer_key Layer key of the preset directory.
 * @return The name of the reference, empty when the key carries no name.
 */
std::wstring KnownFolderReferenceName(const std::wstring& layer_key);

/**
 * @brief Replace the absolute paths of a text which point into a preset directory.
 *
 * The call is the counterpart of the expansion of the sandbox: a value which
 * spells the path of a folder out is rewritten to the reference of that
 * folder, so the archive stays correct on a machine whose folders are
 * somewhere else.
 *
 * A path is replaced when
 *
 * - it equals the real path of a preset directory, or it starts with that path
 *   and continues with a separator, so `C:\Program FilesX` is left alone,
 * - it starts at the beginning of the text or behind a character which is
 *   neither a letter, a digit, an underscore, a dot nor a separator, and
 * - the longest path wins when several preset directories match, which is what
 *   tells the nested ones apart (`#ProgramFilesCommon#` hangs below
 *   `#ProgramFiles#`).
 *
 * The remainder of the path keeps its own separator, so
 * `C:\Program Files\Foo\Bar.exe` becomes `%APPBOX:ProgramFiles%\Foo\Bar.exe`,
 * exactly like the expansion of the sandbox rebuilds it.
 *
 * @param[in] text Text which may spell a path of a preset directory out.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[out] changed Whether at least one path was replaced.
 * @return The text with every path replaced.
 */
std::wstring RewriteKnownFolderReferences(const std::wstring& text, const std::vector<PresetDirectory>& presets,
                                          bool& changed);

/**
 * @brief Replace the absolute paths of the data of a registry value.
 *
 * Only the string types are rewritten: `REG_SZ` and `REG_EXPAND_SZ` hold one
 * text and `REG_MULTI_SZ` holds a list of them whose items are rewritten on
 * their own. Every other type keeps its bytes, even when they happen to spell
 * a path, exactly like the expansion of the sandbox leaves them alone.
 *
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[out] changed Whether at least one path was replaced.
 * @return The data of the value with every path replaced.
 */
std::vector<std::uint8_t> RewriteRegistryValueReferences(std::uint32_t type, const std::vector<std::uint8_t>& data,
                                                         const std::vector<PresetDirectory>& presets, bool& changed);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_KNOWN_FOLDER_REFERENCE_HPP
