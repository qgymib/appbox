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
 * demand. Only executable files can be selected and exactly one of them is the
 * startup file of the packaged application.
 *
 * The class holds no wxWidgets dependency, so the tree and the single
 * selection rule are unit testable.
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
     * @brief Find the row of a startup file choice.
     *
     * The preset and the import are matched by name ignoring the case, the
     * folders on the way to the file are expanded on demand.
     *
     * @param[in] choice The startup file to look up.
     * @return The matching row, null when it does not exist.
     */
    StartupNode* FindChoice(const MainProgram& choice);

    /**
     * @brief Whether a row can be selected as the startup file.
     * @param[in] node Row to test.
     * @return true for executable rows.
     */
    static bool IsCheckable(const StartupNode& node);

    /**
     * @brief Pre-select the startup file of an existing model choice.
     *
     * The choice is stored as an identifier, so a row which is not expanded
     * yet reports IsChecked() as soon as it appears.
     *
     * @param[in] choice The startup file to pre-select.
     */
    void Preselect(const MainProgram& choice);

    /**
     * @brief Whether a row is the selected startup file.
     * @param[in] node Row to test.
     * @return true when the row is the selected executable.
     */
    bool IsChecked(const StartupNode& node) const;

    /**
     * @brief Select a row as the startup file.
     *
     * The selection is exclusive: selecting another executable replaces the
     * previous choice.
     *
     * @param[in] node Row to select.
     * @return true when the row is an executable and was selected.
     */
    bool SetChecked(const StartupNode& node);

    /**
     * @brief Drop the startup file selection.
     */
    void ClearChecked();

    /**
     * @brief Whether a startup file is selected.
     * @return true when a startup file is selected.
     */
    bool HasChecked() const;

    /**
     * @brief Get the selected startup file.
     * @return The selected startup file; only valid when HasChecked() returns
     *         true.
     */
    const MainProgram& Checked() const;

private:
    /**
     * @brief Enumerate the host children of one row and sort them.
     * @param[in,out] node Container row to populate.
     */
    void EnumerateChildren(StartupNode& node);

    std::vector<std::unique_ptr<StartupNode>> roots_;
    MainProgram                               checked_;
    bool                                      has_checked_ = false;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_STARTUP_TREE_HPP
