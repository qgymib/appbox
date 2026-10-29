#ifndef APPBOX_COMMON_KNOWN_FOLDER_HPP
#define APPBOX_COMMON_KNOWN_FOLDER_HPP

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
 * A folder is resolved twice, because the operating system shows a different
 * folder to a 32 bit process than to a 64 bit one: `%ProgramFiles%` names
 * `C:\Program Files` in a 64 bit process and `C:\Program Files (x86)` in a 32
 * bit one. The sandbox picks the view which matches the process it is injected
 * into, so a 32 bit application which starts a 64 bit program gives the child
 * the paths of a 64 bit process.
 *
 * The launcher copies the list into the injected configuration, which is why the
 * texts are UTF-8 like the other texts of that document.
 *
 * @note The header is shared by the packer, the launcher and the tests, so it
 *       must not depend on the sandbox configuration; the copy into the
 *       injected configuration is made by the launcher.
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
     * @brief Path the reference is replaced with in a 64 bit process.
     *
     * The real path of the known folder on the machine which runs the sandbox,
     * for example `"C:\\Program Files"`.
     */
    std::string path;

    /**
     * @brief Path the reference is replaced with in a 32 bit process.
     *
     * For example `"C:\\Program Files (x86)"` for the variable
     * `ProgramFiles`. The text is empty while the folder does not exist for a
     * 32 bit process, which is the `Not applicable` row of the table of the
     * variable expansion: the sandbox then keeps the reference as it is
     * spelled, exactly like it keeps a reference to a name it does not know.
     */
    std::string path_x86;
};

/**
 * @brief One folder the packer writes as a reference into the archive.
 *
 * The capture rewrites an absolute path which points into a folder of the
 * machine to the reference of that folder, so the archive stays correct on a
 * machine whose folders are somewhere else.
 *
 * A reference is only written for a folder which a 32 bit process resolves to
 * the same path as a 64 bit one (or which no 32 bit process resolves at all):
 * the capture does not know the bitness of the process which expands the
 * reference later, so a reference whose meaning depends on that bitness would
 * name another directory there. `ProgramFiles` and `ProgramFilesCommon` are
 * therefore not written by the capture; the paths of the 64 bit folders are
 * written as `ProgramFilesX64` and `ProgramFilesCommonX64` instead.
 */
struct KnownFolderReference
{
    /**
     * @brief Name of the reference without the `%APPBOX:` prefix.
     *
     * For example `L"ProgramFilesX64"`.
     */
    std::wstring name;

    /**
     * @brief Real path of the folder on this machine.
     *
     * For example `L"C:\\Program Files"`.
     */
    std::wstring path;
};

/**
 * @brief Get the variables the sandbox expands in the values of the workspace.
 *
 * One entry is produced per known folder of the table which can be resolved on
 * this machine, in the order of the table. A folder whose path cannot be
 * resolved is skipped with a warning, so the other variables stay usable
 * instead of failing the whole start. A folder which cannot be resolved for a
 * 32 bit process leaves the second view empty, which keeps the variable usable
 * in a 64 bit process and reports the reference as it is spelled in a 32 bit
 * one.
 *
 * @return The variables, in the order of the known folder table.
 */
inline std::vector<KnownFolderVariable> KnownFolderVariables();

/**
 * @brief Get the folders the capture writes as references into the archive.
 *
 * The list holds the folders of the table whose 32 bit view is the same folder
 * or no folder at all, in the order of the table. The order is the priority of
 * the rewrite: two entries which name the same path are matched in that order,
 * so the first one wins.
 *
 * @return The folders, in the order of the known folder table.
 */
inline std::vector<KnownFolderReference> KnownFolderReferences();

/**
 * @brief Search for a known folder by name.
 *
 * The name is a `#Name#` delimited layer key such as `#ProgramFiles#`; the
 * comparison is case sensitive and must match the whole key. Only a key which
 * names a layer directory of the packer is found: a key which exists to be
 * expanded in a value of the workspace only (`#ProgramFilesX64#`) names no
 * layer and no folder.
 *
 * @param[in] name Layer key of the folder.
 * @param[out] folder_path Folder path.
 * @return true if the folder is found, false otherwise.
 */
inline bool SearchFolderID(const std::wstring& name, std::wstring& folder_path);

/**
 * @brief Expand a known folder path.
 *
 * A path which starts with a `#Name#` layer key such as
 * `#ProgramFiles#\MyApp\app.exe` is rewritten to the real path of the folder
 * plus the remainder of the path. Every other path, including a plain
 * absolute path, is returned unchanged. The layer key is matched exactly as the
 * table spells it and without a leading separator, so the remainder keeps its
 * own separator and a doubled backslash is never produced. Only a key which
 * names a layer directory of the packer is expanded, like `SearchFolderID`
 * finds only such a key.
 *
 * @param[in] path File path.
 * @return Expanded file path.
 */
inline std::wstring ExpandKnownFolder(const std::wstring& path);

} // namespace appbox

/*
 * The table of the known folders and the resolution of a folder path are the
 * implementation of the functions above, so they live in a detail header which
 * only this header includes.
 */
#include "details/KnownFolderDetail.hpp"

#endif
