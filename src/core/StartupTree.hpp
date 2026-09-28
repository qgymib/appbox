#ifndef APPBOX_PACKER_CORE_STARTUP_TREE_HPP
#define APPBOX_PACKER_CORE_STARTUP_TREE_HPP

#include "PackModel.hpp"
#include <memory>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Kind of one row of the startup file tree.
 */
enum class StartupNodeKind
{
    Preset,    ///< A preset directory of the packer.
    Import,    ///< An imported folder below a preset directory.
    Directory, ///< A host subfolder of an imported folder.
    Executable ///< A host executable below an imported folder.
};

/**
 * @brief One row of the startup file tree.
 *
 * The preset and import rows are built from the pack model, the host
 * subfolders and the executables below an import are enumerated on demand and
 * cached in `children`. Every node owns its children, so the tree is destroyed
 * as a whole.
 */
struct StartupNode
{
    /**
     * @brief Kind of the row.
     */
    StartupNodeKind kind = StartupNodeKind::Directory;

    /**
     * @brief Name shown in the name column.
     */
    std::wstring label;

    /**
     * @brief Host path of the folder or file behind the row.
     */
    std::wstring host_path;

    /**
     * @brief Identifier of the owning preset directory (imports and below).
     */
    std::string preset_id;

    /**
     * @brief Name of the owning imported folder (imports and below).
     */
    std::wstring import_name;

    /**
     * @brief Path relative to the import root using backslashes, empty for
     *        import roots.
     */
    std::wstring relative_path;

    /**
     * @brief Owning node, null for the preset rows.
     */
    StartupNode* parent = nullptr;

    /**
     * @brief Children of the row, enumerated on demand.
     */
    std::vector<std::unique_ptr<StartupNode>> children;

    /**
     * @brief Whether the children of the row are resolved already.
     *
     * The children of a preset row are the imported folders of the model, the
     * host children of the other container rows are enumerated on demand.
     */
    bool populated = false;
};

/**
 * @brief Tree of the imported folders offering their executables as startup files.
 *
 * The tree mirrors the sandbox view of the packer: the preset directories with
 * their imported folders, expanded into the host subfolders of an import on
 * demand. Only executable files can become startup files, and any number of
 * them can be chosen.
 *
 * The tree owns the startup file list the dialog edits: every executable row
 * reports whether it is a startup file, whether it starts automatically and
 * which trigger it carries. The list keeps the order the files were added in,
 * which is the order the sandbox starts them in.
 *
 * The class holds no wxWidgets dependency, so the tree and the rules of the
 * startup file list are unit testable.
 */
class StartupTree
{
public:
    /**
     * @brief Build the tree of the preset and import rows.
     * @param[in] model The pack model holding the imported folders.
     */
    explicit StartupTree(const PackModel& model);

    /**
     * @brief Get the preset rows of the tree.
     * @return The preset rows in preset order.
     */
    const std::vector<std::unique_ptr<StartupNode>>& Roots() const;

    /**
     * @brief Enumerate the host children of one container row.
     *
     * The children are enumerated only once: the `populated` flag of the row
     * makes later calls return immediately. Enumerating a folder which cannot
     * be read leaves the row without children.
     *
     * @param[in,out] node Container row to populate.
     */
    void EnsureChildren(StartupNode& node);

    /**
     * @brief Find the row of a path below an import root.
     *
     * The path is split at the backslashes and every level is expanded on
     * demand, so the lookup works without expanding the tree before.
     *
     * @param[in,out] import_root The import row to search below.
     * @param[in] relative_path Path relative to the import root.
     * @return The matching row, null when it does not exist.
     */
    StartupNode* FindNode(StartupNode& import_root, const std::wstring& relative_path);

    /**
     * @brief Find the row of a startup file.
     *
     * The preset and the import are matched by name ignoring the case, the
     * folders on the way to the file are expanded on demand.
     *
     * @param[in] file The startup file to look up.
     * @return The matching row, null when it does not exist.
     */
    StartupNode* FindChoice(const StartupFile& file);

    /**
     * @brief Whether a row can be a startup file.
     * @param[in] node Row to test.
     * @return true for executable rows.
     */
    static bool IsCheckable(const StartupNode& node);

    /**
     * @brief Replace the startup file list with the model content.
     *
     * The list is stored as an identifier, so a row which is not expanded yet
     * reports its state as soon as it appears.
     *
     * @param[in] files The startup files of the model.
     */
    void Preselect(const std::vector<StartupFile>& files);

    /**
     * @brief Get the startup file list of the tree.
     * @return The startup files in startup order.
     */
    const std::vector<StartupFile>& Files() const;

    /**
     * @brief Whether the tree holds at least one startup file.
     * @return true when a startup file is in the list.
     */
    bool HasFiles() const;

    /**
     * @brief Whether a row is a startup file.
     * @param[in] node Row to test.
     * @return true when the row is in the startup file list.
     */
    bool Contains(const StartupNode& node) const;

    /**
     * @brief Whether a row starts automatically.
     * @param[in] node Row to test.
     * @return true when the row is a startup file with the auto start flag.
     */
    bool IsAutoStart(const StartupNode& node) const;

    /**
     * @brief Get the trigger of a row.
     * @param[in] node Row to test.
     * @return The trigger of the row, empty when it is not a startup file.
     */
    std::wstring TriggerOf(const StartupNode& node) const;

    /**
     * @brief Set the auto start flag of a row.
     *
     * A row which is not a startup file yet is appended to the list with the
     * default trigger. Clearing the flag of a row which is not in the list
     * does nothing.
     *
     * @param[in] node Row to change.
     * @param[in] auto_start New state of the flag.
     * @return true when the row is an executable and the list was changed.
     */
    bool SetAutoStart(const StartupNode& node, bool auto_start);

    /**
     * @brief Set the trigger of a row.
     *
     * A row which is not a startup file yet is appended to the list. The
     * trigger must not be empty and must not be used by another startup file
     * of the tree, ignoring case.
     *
     * @param[in] node Row to change.
     * @param[in] trigger New trigger of the row.
     * @param[out] error Error description on failure.
     * @return true when the row is an executable and the trigger was stored.
     */
    bool SetTrigger(const StartupNode& node, const std::wstring& trigger, std::string& error);

    /**
     * @brief Drop a row from the startup file list.
     * @param[in] node Row to drop.
     * @return true when the row was a startup file and was dropped.
     */
    bool Remove(const StartupNode& node);

private:
    /**
     * @brief Enumerate the host children of one row and sort them.
     * @param[in,out] node Container row to populate.
     */
    void EnumerateChildren(StartupNode& node);

    /**
     * @brief Whether one row and one startup file describe the same executable.
     * @param[in] node Row to test.
     * @param[in] file Startup file to test.
     * @return true when both name the same executable of the same import.
     */
    static bool Matches(const StartupNode& node, const StartupFile& file);

    /**
     * @brief Build the startup file of a row.
     * @param[in] node Executable row.
     * @return The startup file of the row without its trigger.
     */
    static StartupFile MakeStartupFile(const StartupNode& node);

    /**
     * @brief Get the index of a row inside the startup file list.
     * @param[in] node Row to look up.
     * @return The index, -1 when the row is not a startup file.
     */
    int IndexOf(const StartupNode& node) const;

    std::vector<std::unique_ptr<StartupNode>> roots_;
    std::vector<StartupFile>                  files_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_STARTUP_TREE_HPP
