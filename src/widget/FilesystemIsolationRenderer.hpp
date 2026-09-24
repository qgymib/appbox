#ifndef APPBOX_PACKER_WIDGET_FILESYSTEM_ISOLATION_RENDERER_HPP
#define APPBOX_PACKER_WIDGET_FILESYSTEM_ISOLATION_RENDERER_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/dataview.h>
#include <functional>

/**
 * @brief Renderer of the Isolation column of the filesystem table.
 *
 * The cell is edited through a dropdown, like the isolation column of the
 * registry table, but the options depend on the row: a folder offers `Full`,
 * `Write Copy` and `Whiteout` while a file offers `Full` and `Whiteout` only.
 *
 * The choice list of a wxDataViewChoiceRenderer belongs to the column and not
 * to the row, so the renderer asks the panel for the options of the row which
 * is about to be edited. The renderer base class stores the item it starts to
 * edit before it asks for the editor control, which is what makes that lookup
 * possible.
 */
class FilesystemIsolationRenderer : public wxDataViewChoiceRenderer
{
public:
    /**
     * @brief Signature of the callback which reports the options of a row.
     */
    using ChoicesCallback = std::function<wxArrayString(const wxDataViewItem&)>;

    /**
     * @brief Create the isolation renderer.
     * @param[in] choices Options of the column, used to measure the cell.
     * @param[in] row_choices Callback which reports the options of a row.
     */
    FilesystemIsolationRenderer(const wxArrayString& choices, ChoicesCallback row_choices);

    /**
     * @brief Create the dropdown of the cell which is edited.
     * @param[in] parent Parent window of the dropdown.
     * @param[in] labelRect Rectangle of the cell.
     * @param[in] value Value the cell shows.
     * @return The dropdown control.
     */
    wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) override;

private:
    /**
     * @brief Callback which reports the options of a row.
     */
    ChoicesCallback row_choices_;
};

#endif // APPBOX_PACKER_WIDGET_FILESYSTEM_ISOLATION_RENDERER_HPP
