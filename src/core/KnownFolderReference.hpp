#ifndef APPBOX_PACKER_CORE_KNOWN_FOLDER_REFERENCE_HPP
#define APPBOX_PACKER_CORE_KNOWN_FOLDER_REFERENCE_HPP

#include "utils/KnownFolder.hpp"
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
 * @brief Replace the absolute paths of a text which point into a known folder.
 *
 * The call is the counterpart of the expansion of the sandbox: a value which
 * spells the path of a folder out is rewritten to the reference of that
 * folder, so the archive stays correct on a machine whose folders are
 * somewhere else.
 *
 * A path is replaced when
 *
 * - it equals the real path of a folder of the table, or it starts with that
 *   path and continues with a character which is not part of a word, so
 *   `C:\Program FilesX` is left alone,
 * - it starts at the beginning of the text or behind a character which is
 *   neither a letter, a digit, an underscore nor a dot, and
 * - the longest path wins when several folders match, which is what tells the
 *   nested ones apart (`C:\Program Files\Common Files` is
 *   `%APPBOX:ProgramFilesCommonX64%` and not a folder of
 *   `%APPBOX:ProgramFilesX64%`).
 *
 * The name of the reference is the one the table of the folders carries, so the
 * capture writes a reference whose meaning does not depend on the bitness of
 * the process which expands it: `C:\Program Files\Foo\Bar.exe` becomes
 * `%APPBOX:ProgramFilesX64%\Foo\Bar.exe`, which names the 64 bit folder in a 64
 * bit process and keeps its spelling in a 32 bit one. The remainder of the path
 * keeps its own separator, exactly like the expansion of the sandbox rebuilds
 * it.
 *
 * @param[in] text Text which may spell a path of a known folder out.
 * @param[in] references Folders which are resolved on this machine.
 * @param[out] changed Whether at least one path was replaced.
 * @return The text with every path replaced.
 */
std::wstring RewriteKnownFolderReferences(const std::wstring& text, const std::vector<KnownFolderReference>& references,
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
 * @param[in] references Folders which are resolved on this machine.
 * @param[out] changed Whether at least one path was replaced.
 * @return The data of the value with every path replaced.
 */
std::vector<std::uint8_t> RewriteRegistryValueReferences(std::uint32_t type, const std::vector<std::uint8_t>& data,
                                                         const std::vector<KnownFolderReference>& references,
                                                         bool&                                    changed);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_KNOWN_FOLDER_REFERENCE_HPP
