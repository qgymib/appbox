#ifndef APPBOX_LAUNCHER_UTILS_KNOWNFOLDER_HPP
#define APPBOX_LAUNCHER_UTILS_KNOWNFOLDER_HPP

#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One variable the sandbox expands in a value of the workspace.
 *
 * A value the user enters in the Environment or the Registry workspace may
 * reference a known folder of the machine the sandbox runs on with the syntax
 * `%APPBOX:<NAME>%` instead of spelling its path out. The name is the layer key
 * of a preset directory without its `#` delimiters, so `#ProgramFiles#` becomes
 * the variable `ProgramFiles`; the supported list therefore follows the table
 * of the known folders below, and a preset directory which is added later
 * brings its variable with it.
 *
 * The launcher copies the list into the injected configuration, which is why the
 * texts are UTF-8 like the other texts of that document.
 *
 * @note The header is included by the packer as well, so it must not depend on
 *       the sandbox configuration; the copy into the injected configuration is
 *       made by the launcher.
 */
struct KnownFolderVariable
{
    /**
     * @brief Name of the variable without the `%APPBOX:` prefix.
     *
     * For example `"ProgramFiles"`. The sandbox compares the name of a
     * reference with this name ignoring the case.
     */
    std::string name;

    /**
     * @brief Path the reference is replaced with.
     *
     * The real path of the known folder on the machine which runs the sandbox,
     * for example `"C:\\Program Files"`.
     */
    std::string path;
};

/**
 * @brief Get the variables the sandbox expands in the values of the workspace.
 *
 * One entry is produced per known folder of the table which can be resolved on
 * this machine, in the order of the table. A folder whose path cannot be
 * resolved is skipped with a warning, so the other variables stay usable
 * instead of failing the whole start.
 *
 * @return The variables, in the order of the known folder table.
 */
std::vector<KnownFolderVariable> KnownFolderVariables();

/**
 * @brief Search for a known folder by name.
 *
 * The name is a `#Name#` delimited layer key such as `#ProgramFiles#`; the
 * comparison is case sensitive and must match the whole key.
 *
 * @param[in] name Layer key of the folder.
 * @param[out] folder_path Folder path.
 * @return true if the folder is found, false otherwise.
 */
bool SearchFolderID(const std::wstring& name, std::wstring& folder_path);

/**
 * @brief Expand a known folder path.
 *
 * A path which starts with a `#Name#` layer key such as
 * `#ProgramFiles#\MyApp\app.exe` is rewritten to the real path of the folder
 * plus the remainder of the path. Every other path, including a plain
 * absolute path, is returned unchanged. The layer key is matched case
 * insensitively and without a leading separator, so the remainder keeps its
 * own separator and a doubled backslash is never produced.
 *
 * @param[in] path File path.
 * @return Expanded file path.
 */
std::wstring ExpandKnownFolder(const std::wstring& path);

} // namespace appbox

#endif
