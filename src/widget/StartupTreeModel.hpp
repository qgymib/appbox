#ifndef APPBOX_PACKER_WIDGET_STARTUP_TREE_MODEL_HPP
#define APPBOX_PACKER_WIDGET_STARTUP_TREE_MODEL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/dataview.h>
#include "core/StartupTree.hpp"
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Tree model of the startup file browser.
 *
 * The model presents the startup file tree of the packer as a tree table with
 * the columns Name, Type, Auto Start and Trigger. The tree is built by a
 * background scan, because it reads the host folders of the imported folders
 * and keeps only the folders on the way to a startable file; the model is
 * empty until the scan hands its result over, see Adopt().
 *
 * The Auto Start and Trigger columns report a value for the rows which name a
 * startable file only; the rows which cannot be a startup file report no value
 * at all, which keeps their cells empty and non clickable. Checking the box of
 * a startable file adds it to the startup file list, clearing the box keeps it
 * in the list but stops the sandbox from starting it on its own.
 */
class StartupTreeModel : public wxDataViewModel
{
public:
    /**
     * @brief Column layout of the startup file tree.
     */
    enum Column
    {
        NameColumn = 0,      ///< Tree column with the icon and the name of the row.
        TypeColumn = 1,      ///< Folder or Executable.
        AutoStartColumn = 2, ///< Checkbox marking the automatic start.
        TriggerColumn = 3    ///< Trigger name of a startup file.
    };

    /**
     * @brief Create the model of a scan which did not finish yet.
     *
     * The control starts with no row at all, which is the state the dialog
     * shows while it reports that it is scanning the folders of the view.
     */
    StartupTreeModel();

    /**
     * @brief Take over the tree the background scan built.
     *
     * The control is told that the whole model was replaced, so it shows the
     * rows of the new tree.
     *
     * @param[in] tree The finished tree, which the model keeps.
     */
    void Adopt(const std::shared_ptr<appbox::StartupTree>& tree);

    /**
     * @brief Whether the background scan handed its tree over already.
     * @return true when the model holds a tree.
     */
    bool IsReady() const;

    /**
     * @brief Get the rows the control has to expand.
     *
     * The dialog expands every row it gets, so it opens with the whole tree in
     * view instead of the rows on the way to a startup file only.
     *
     * @return The rows which hold children, empty while the scan runs.
     */
    std::vector<const appbox::StartupNode*> ExpandableRows() const;

    /**
     * @brief Get the row behind one data view item.
     * @param[in] item Data view item of the row.
     * @return The row, null for the hidden root or an invalid item.
     */
    appbox::StartupNode* Node(const wxDataViewItem& item) const;

    /**
     * @brief Get the data view item of one row.
     * @param[in] node Row to look up, may be null.
     * @return The data view item, invalid when the row is null.
     */
    wxDataViewItem Item(const appbox::StartupNode* node) const;

    /**
     * @brief Find the row of a startup file.
     * @param[in] file The startup file to look up.
     * @return The row, null when it does not exist.
     */
    appbox::StartupNode* FindChoice(const appbox::StartupFile& file) const;

    /**
     * @brief Whether the tree holds at least one startup file.
     * @return true when a startup file is in the list.
     */
    bool HasFiles() const;

    /**
     * @brief Get the startup file list of the tree.
     * @return The startup files in startup order.
     */
    const std::vector<appbox::StartupFile>& Files() const;

    /**
     * @brief Whether a row is a startup file.
     * @param[in] item Data view item of the row.
     * @return true when the row is in the startup file list.
     */
    bool IsStartupFile(const wxDataViewItem& item) const;

    /**
     * @brief Whether a row starts automatically.
     * @param[in] item Data view item of the row.
     * @return true when the row is a startup file with the auto start flag.
     */
    bool IsAutoStart(const wxDataViewItem& item) const;

    /**
     * @brief Drop a row from the startup file list.
     *
     * The cells of the row are reported as changed, so the checkbox and the
     * trigger disappear without a rebuild of the table.
     *
     * @param[in] item Data view item of the row.
     * @return true when the row was a startup file and was dropped.
     */
    bool Remove(const wxDataViewItem& item);

    /**
     * @brief Get and clear the description of the last rejected value.
     *
     * The dialog shows the description and asks the control to re-read the
     * cell, which restores the value the tree kept.
     *
     * @return The description of the last rejected value, empty when the last
     *         value was accepted.
     */
    wxString TakeError();

    /**
     * @brief Get the value of one cell.
     * @param[out] variant Value of the cell, empty for a row without a startup
     *             file.
     * @param[in] item Data view item of the row.
     * @param[in] col Column index.
     */
    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;

    /**
     * @brief Store the value of one cell.
     * @param[in] variant New value of the cell.
     * @param[in] item Data view item of the row.
     * @param[in] col Column index.
     * @return true when the value was stored.
     */
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    /**
     * @brief Get the parent row of one row.
     * @param[in] item Data view item of the row.
     * @return The data view item of the parent, invalid for the container row.
     */
    wxDataViewItem GetParent(const wxDataViewItem& item) const override;

    /**
     * @brief Whether one row can have children.
     * @param[in] item Data view item of the row.
     * @return true for the container, the preset, the import and the folder rows.
     */
    bool IsContainer(const wxDataViewItem& item) const override;

    /**
     * @brief Whether the children of a container use every column.
     *
     * Without this the control would show the children of a container in the
     * name column only.
     *
     * @param[in] item Data view item of the row.
     * @return Always true.
     */
    bool HasContainerColumns(const wxDataViewItem& item) const override;

    /**
     * @brief Get the children of one row.
     * @param[in] item Data view item of the row, invalid for the roots.
     * @param[out] children Children of the row.
     * @return The number of children.
     */
    unsigned int GetChildren(const wxDataViewItem& item, wxDataViewItemArray& children) const override;

private:
    /**
     * @brief Get the icon shown in the name column of one row.
     * @param[in] node Row to inspect.
     * @return The icon of the row.
     */
    const wxBitmapBundle& IconOf(const appbox::StartupNode& node) const;

    /**
     * @brief Get the type label of one row.
     * @param[in] node Row to inspect.
     * @return The label of the type column.
     */
    static wxString TypeLabel(const appbox::StartupNode& node);

    /**
     * @brief The tree of the finished scan, null while the scan runs.
     */
    std::shared_ptr<appbox::StartupTree> tree_;

    /**
     * @brief Icon of a folder row.
     */
    wxBitmapBundle folder_icon_;

    /**
     * @brief Icon of a row which names a startable file.
     */
    wxBitmapBundle executable_icon_;

    /**
     * @brief Description of the last rejected value.
     */
    wxString error_;
};

#endif // APPBOX_PACKER_WIDGET_STARTUP_TREE_MODEL_HPP
