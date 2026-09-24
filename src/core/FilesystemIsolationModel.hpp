#ifndef APPBOX_PACKER_CORE_FILESYSTEM_ISOLATION_MODEL_HPP
#define APPBOX_PACKER_CORE_FILESYSTEM_ISOLATION_MODEL_HPP

#include "FilesystemIsolation.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Separator of the paths of the virtual filesystem.
 *
 * The paths of the view are the strings the `Source Path` column shows, for
 * example `#ProgramFiles#\MyApp\app.exe`: the first component is the layer key
 * of the preset directory and the remaining ones are the path below it.
 */
inline constexpr wchar_t kFilesystemViewPathSeparator = L'\\';

/**
 * @brief Get the display names of the isolation modes.
 *
 * The list is ordered like the enumeration of the modes, so the position of a
 * name is the mode itself. It is the single source of the names the dropdown
 * of the filesystem table offers and the names the tests assert.
 *
 * @return The display names, one per mode.
 */
const std::vector<std::wstring>& FilesystemIsolationNames();

/**
 * @brief Get the display name of one isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The display name of the mode.
 */
std::wstring FilesystemIsolationName(FilesystemIsolation isolation);

/**
 * @brief Get the display names of the modes an entry kind accepts.
 *
 * A folder accepts `Full`, `Write Copy` and `Whiteout`; a file accepts `Full`
 * and `Whiteout` only, so the dropdown of a file row never offers a mode the
 * entry cannot hold.
 *
 * @param[in] kind The kind of the entry.
 * @return The display names in display order.
 */
const std::vector<std::wstring>& FilesystemIsolationNamesFor(FilesystemEntryKind kind);

/**
 * @brief Resolve an isolation mode from its display name.
 *
 * The comparison ignores the case, so `L"write copy"` resolves as well.
 *
 * @param[in] name The display name to resolve.
 * @param[out] out The resolved mode when the name is known.
 * @return true when the name names an isolation mode.
 */
bool ParseFilesystemIsolationName(const std::wstring& name, FilesystemIsolation& out);

/**
 * @brief Get the default isolation mode of an entry kind.
 *
 * A folder which the user never touched is `WriteCopy`, a file is `Full`.
 *
 * @param[in] kind The kind of the entry.
 * @return The default mode of the kind.
 */
FilesystemIsolation DefaultFilesystemIsolation(FilesystemEntryKind kind);

/**
 * @brief Express an isolation mode in the modes of an entry kind.
 *
 * `WriteCopy` describes the merge of a folder with the host filesystem, which
 * a single file cannot express: for a file it behaves like `Full`, because
 * both keep the host content visible and send every write into the sandbox.
 * Every other mode is returned unchanged.
 *
 * @param[in] isolation The mode to express.
 * @param[in] kind The kind of the entry.
 * @return The mode as seen by the kind.
 */
FilesystemIsolation FilesystemIsolationForKind(FilesystemIsolation isolation, FilesystemEntryKind kind);

/**
 * @brief Split a virtual path into its components.
 *
 * Both separators are accepted and empty components are dropped, so
 * `L"#ProgramFiles#\\MyApp"` and `L"#ProgramFiles#/MyApp"` yield the same
 * components.
 *
 * @param[in] path The path to split.
 * @return The components in path order, empty for an empty path.
 */
std::vector<std::wstring> SplitViewPath(const std::wstring& path);

/**
 * @brief Join a parent path and a name into a path.
 * @param[in] parent Path of the parent folder, empty for a layer root.
 * @param[in] name Name of the entry inside the parent.
 * @return The joined path.
 */
std::wstring JoinViewPath(const std::wstring& parent, const std::wstring& name);

/**
 * @brief Get the path of the parent of an entry.
 * @param[in] path Path of the entry.
 * @return The parent path, empty for a layer root and for an empty path.
 */
std::wstring ViewPathParent(const std::wstring& path);

/**
 * @brief Get the name of an entry inside its parent.
 * @param[in] path Path of the entry.
 * @return The last component of the path, empty for an empty path.
 */
std::wstring ViewPathLeafName(const std::wstring& path);

/**
 * @brief Normalize a path to the separator and shape used by the model.
 *
 * Forward slashes become backslashes, empty and current directory components
 * are dropped and a parent reference is rejected, because the result is used
 * as a path of the virtual filesystem.
 *
 * @param[in] path The path to normalize.
 * @return The normalized path, empty when it is not usable.
 */
std::wstring NormalizeViewPath(const std::wstring& path);

/**
 * @brief Compare two virtual paths ignoring the case.
 *
 * The host filesystem is case insensitive, so `#ProgramFiles#\MyApp` and
 * `#programfiles#\myapp` name the same entry of the view.
 *
 * @param[in] left Left path.
 * @param[in] right Right path.
 * @return true when both paths name the same entry.
 */
bool ViewPathEquals(const std::wstring& left, const std::wstring& right);

/**
 * @brief Whether a path is an entry below another one.
 *
 * The ancestor has to be a whole component prefix, so
 * `#ProgramFiles#\MyAppData` is not below `#ProgramFiles#\MyApp`.
 *
 * @param[in] path Path of the entry to test.
 * @param[in] ancestor Path of the folder to test against.
 * @return true when the path is a strict descendant of the ancestor.
 */
bool IsViewPathBelow(const std::wstring& path, const std::wstring& ancestor);

/**
 * @brief One entry of the isolation table of the virtual filesystem.
 *
 * The entry records the mode the user picked for one path of the view. Paths
 * the user never touched have no entry at all and follow the closest folder
 * above them, which is what makes the mode of a folder reach the entries below
 * it.
 */
struct FilesystemIsolationEntry
{
    /**
     * @brief Normalized path of the entry inside the virtual filesystem.
     */
    std::wstring path;

    /**
     * @brief Kind of the entry the mode was picked for.
     */
    FilesystemEntryKind kind = FilesystemEntryKind::Directory;

    /**
     * @brief Isolation mode the user picked for the entry.
     */
    FilesystemIsolation isolation = FilesystemIsolation::WriteCopy;
};

/**
 * @brief Editable isolation modes of the virtual filesystem of the packer.
 *
 * The model holds the modes the user picked for the files and folders of the
 * filesystem workspace and resolves the mode of every other entry by
 * inheritance: an entry without a mode of its own follows the closest folder
 * above it which carries one, and an entry without any such folder follows the
 * default of its kind (`WriteCopy` for a folder, `Full` for a file).
 *
 * The class holds no wxWidgets dependency and never touches the host
 * filesystem, so the inheritance rules are unit testable.
 *
 * Every operation which can fail validates its input first and reports an
 * English error description without changing the model.
 */
class FilesystemIsolationModel
{
public:
    /**
     * @brief Drop every isolation mode.
     *
     * The model is left in the state of a fresh session, in which every entry
     * follows the default of its kind.
     */
    void Reset();

    /**
     * @brief Whether the model holds no mode of its own.
     * @return true when every entry follows its default.
     */
    bool IsEmpty() const;

    /**
     * @brief Set the isolation mode of one entry.
     *
     * The mode reaches the entry itself; the entries below it keep their own
     * modes and follow the new one only when they carry none of their own.
     * An entry which already holds a mode is updated in place.
     *
     * The call fails when the path does not name an entry of the view or when
     * the mode cannot be used for the kind of the entry.
     *
     * @param[in] view_path Path of the entry inside the virtual filesystem.
     * @param[in] kind Kind of the entry.
     * @param[in] isolation New isolation mode.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetIsolation(const std::wstring& view_path, FilesystemEntryKind kind,
                      FilesystemIsolation isolation, std::string& error);

    /**
     * @brief Add one entry exactly as it is recorded.
     *
     * Unlike SetIsolation(), which updates an entry the user picks a mode for,
     * this entry point restores an entry of a stored document and therefore
     * refuses a path which is already listed.
     *
     * @param[in] entry The entry to add.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddEntry(const FilesystemIsolationEntry& entry, std::string& error);

    /**
     * @brief Drop the mode of a path and of everything below it.
     *
     * The call is used when an import is removed: the entries of the removed
     * subtree have to go with it, otherwise a folder imported under the same
     * name later would silently inherit the stale mode.
     *
     * @param[in] view_path Path of the entry to drop.
     * @return true when at least one entry was removed.
     */
    bool RemoveSubtree(const std::wstring& view_path);

    /**
     * @brief Whether a path carries a mode of its own.
     * @param[in] view_path Path of the entry inside the virtual filesystem.
     * @return true when the model holds an entry for the path.
     */
    bool HasExplicitIsolation(const std::wstring& view_path) const;

    /**
     * @brief Get the isolation mode which applies to an entry.
     *
     * The mode of the entry itself wins; without one the closest folder above
     * it which carries a mode decides, and without any such folder the default
     * of the kind is returned. A folder above which is hidden by a `Whiteout`
     * hides the entry as well, which is why a file below it reports
     * `Whiteout`; a folder which is merged with the host (`Full` or
     * `WriteCopy`) reports `Full` for a file.
     *
     * @param[in] view_path Path of the entry inside the virtual filesystem.
     * @param[in] kind Kind of the entry.
     * @return The isolation mode which applies to the entry.
     */
    FilesystemIsolation EffectiveIsolation(const std::wstring& view_path, FilesystemEntryKind kind) const;

    /**
     * @brief Get every entry the model holds.
     *
     * The entries are ordered by their path, so the document which is written
     * from them is stable for a given model.
     *
     * @return The entries in path order.
     */
    const std::vector<FilesystemIsolationEntry>& Entries() const;

private:
    /**
     * @brief Find the position of an entry.
     * @param[in] normalized_path Normalized path to look for.
     * @return The index of the entry, -1 when it does not exist.
     */
    std::ptrdiff_t EntryIndex(const std::wstring& normalized_path) const;

    /**
     * @brief Restore the path order of the entries.
     */
    void SortEntries();

    /**
     * @brief The modes the user picked, ordered by path.
     */
    std::vector<FilesystemIsolationEntry> entries_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_FILESYSTEM_ISOLATION_MODEL_HPP
