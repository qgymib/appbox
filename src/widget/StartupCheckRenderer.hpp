#ifndef APPBOX_PACKER_WIDGET_STARTUP_CHECK_RENDERER_HPP
#define APPBOX_PACKER_WIDGET_STARTUP_CHECK_RENDERER_HPP

#include <wx/dataview.h>

/**
 * @brief Renderer of the Startup column of the startup file tree.
 *
 * The cell draws a native checkbox while the model reports a value for the
 * row. Rows which cannot be selected as the startup file report no value at
 * all: wxDataViewCtrl then leaves their cell empty and never calls the
 * renderer for them, so a folder row does not show a checkbox.
 *
 * Clicking or activating the cell toggles the value through the model, which
 * keeps the selection exclusive.
 */
class StartupCheckRenderer : public wxDataViewCustomRenderer
{
public:
    /**
     * @brief Create the checkbox renderer.
     */
    StartupCheckRenderer();

    /**
     * @brief Draw the checkbox of one cell.
     * @param[in] cell Rectangle of the cell.
     * @param[in] dc Device context to draw on.
     * @param[in] state Cell state flags.
     * @return true, the cell was drawn.
     */
    bool Render(wxRect cell, wxDC* dc, int state) override;

    /**
     * @brief Get the size of the checkbox.
     * @return The size of the checkbox in pixels.
     */
    wxSize GetSize() const override;

    /**
     * @brief Store the value of the row about to be drawn.
     * @param[in] value Value reported by the model.
     * @return true when the value was a checkbox state.
     */
    bool SetValue(const wxVariant& value) override;

    /**
     * @brief Get the value held by the renderer.
     * @param[out] value The checkbox state.
     * @return true when a value is held.
     */
    bool GetValue(wxVariant& value) const override;

#if wxUSE_ACCESSIBILITY
    /**
     * @brief Describe the cell for the screen readers.
     * @return The accessibility description of the cell.
     */
    wxString GetAccessibleDescription() const override;
#endif // wxUSE_ACCESSIBILITY

    /**
     * @brief Toggle the checkbox of one cell.
     *
     * The value is read back from the model because an empty cell never
     * reaches the renderer: rows without a startup file cannot be toggled.
     *
     * @param[in] cell Rectangle of the cell.
     * @param[in,out] model Model of the control.
     * @param[in] item Item of the activated cell.
     * @param[in] col Column of the activated cell.
     * @param[in] mouseEvent Mouse event which activated the cell, may be null.
     * @return true when the value was changed.
     */
    bool ActivateCell(const wxRect& cell, wxDataViewModel* model, const wxDataViewItem& item,
                      unsigned int col, const wxMouseEvent* mouseEvent) override;

private:
    /**
     * @brief Checkbox state of the row being drawn.
     */
    bool checked_ = false;
};

#endif // APPBOX_PACKER_WIDGET_STARTUP_CHECK_RENDERER_HPP
