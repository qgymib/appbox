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
#include "core/PackModel.hpp"
#include "core/StartupTree.hpp"

/**
 * @brief Tree model of the startup file browser.
 *
 * The model presents the startup file tree of the pack model as a tree table
 * with the columns Name, Type and Startup. Host subfolders are enumerated by
 * the tree on demand, so a folder is read only once it is shown for the first
 * time.
 *
 * The Startup column reports a checkbox state for executable rows only; the
 * rows which cannot be the startup file report no value at all, which keeps
 * their cell empty and non clickable.
 */
class StartupTreeModel : public wxDataViewModel
{
public:
    /**
     * @brief Column layout of the startup file tree.
     */
    enum Column
    {
        NameColumn = 0,   ///< Tree column with the icon and the name of the row.
        TypeColumn = 1,   ///< Folder or Executable.
        StartupColumn = 2 ///< Checkbox marking the startup file.
    };

    /**
     * @brief Build the model of the imported folders.
     * @param[in] model The pack model holding the imported folders.
     */
    explicit StartupTreeModel(const appbox::PackModel& model);

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
     * @brief Find the row of a startup file choice.
     *
     * The folders on the way to the row are enumerated, so the row can be
     * expanded and shown without expanding the tree by hand.
     *
     * @param[in] choice The startup file to look up.
     * @return The row, null when it does not exist.
     */
    appbox::StartupNode* FindChoice(const appbox::MainProgram& choice);

    /**
     * @brief Pre-select the startup file of an existing model choice.
     * @param[in] choice The startup file to pre-select.
     */
    void Preselect(const appbox::MainProgram& choice);

    /**
     * @brief Select one row as the startup file.
     *
     * The selection is exclusive: the previously checked row is repainted as
     * well.
     *
     * @param[in] item Data view item of the row to select.
     * @return true when the row is an executable and was selected.
     */
    bool SetChecked(const wxDataViewItem& item);

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
    appbox::MainProgram Checked() const;

    /**
     * @brief Get the value of one cell.
     * @param[out] variant Value of the cell, null for a row without a startup
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
     * @return The data view item of the parent, invalid for the preset rows.
     */
    wxDataViewItem GetParent(const wxDataViewItem& item) const override;

    /**
     * @brief Whether one row can have children.
     * @param[in] item Data view item of the row.
     * @return true for the preset, import and folder rows.
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
     * @brief Tree of the imports, expanded on demand.
     */
    mutable appbox::StartupTree tree_;

    /**
     * @brief Icon of a folder row.
     */
    wxBitmapBundle folder_icon_;

    /**
     * @brief Icon of an executable row.
     */
    wxBitmapBundle executable_icon_;

    /**
     * @brief Data view item of the checked row, invalid while nothing is
     *        checked or before the checked row was created.
     */
    wxDataViewItem checked_item_;
};

#endif // APPBOX_PACKER_WIDGET_STARTUP_TREE_MODEL_HPP
