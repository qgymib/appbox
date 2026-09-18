#include "StartupCheckRenderer.hpp"
#include <wx/renderer.h>

namespace
{

/**
 * @brief Get the control a renderer belongs to.
 * @param[in] renderer Renderer to inspect.
 * @return The owning control, null while the renderer is not attached.
 */
wxDataViewCtrl* OwningView(const wxDataViewRenderer& renderer)
{
    wxDataViewColumn* const column = renderer.GetOwner();
    return column != nullptr ? column->GetOwner() : nullptr;
}

} // namespace

StartupCheckRenderer::StartupCheckRenderer()
    : wxDataViewCustomRenderer("bool", wxDATAVIEW_CELL_ACTIVATABLE, wxALIGN_CENTER)
{
}

bool StartupCheckRenderer::Render(wxRect cell, wxDC* dc, int)
{
    wxDataViewCtrl* const view = OwningView(*this);
    if (view == nullptr || dc == nullptr)
    {
        return false;
    }

    int flags = checked_ ? wxCONTROL_CHECKED : 0;
    if (GetMode() != wxDATAVIEW_CELL_ACTIVATABLE || !GetEnabled())
    {
        flags |= wxCONTROL_DISABLED;
    }

    /*
     * DrawCheckBox() does not render boxes below the minimal size correctly,
     * so the box is enlarged when the cell is too small.
     */
    wxSize size = cell.GetSize();
    size.IncTo(GetSize());
    cell.SetSize(size);

    wxRendererNative::Get().DrawCheckBox(view, *dc, cell, flags);
    return true;
}

wxSize StartupCheckRenderer::GetSize() const
{
    wxDataViewCtrl* const view = OwningView(*this);
    return view != nullptr ? view->FromDIP(wxSize(16, 16)) : wxSize(16, 16);
}

bool StartupCheckRenderer::SetValue(const wxVariant& value)
{
    checked_ = value.GetBool();
    return true;
}

bool StartupCheckRenderer::GetValue(wxVariant& value) const
{
    value = checked_;
    return true;
}

#if wxUSE_ACCESSIBILITY
wxString StartupCheckRenderer::GetAccessibleDescription() const
{
    return checked_ ? "startup file" : "not the startup file";
}
#endif // wxUSE_ACCESSIBILITY

bool StartupCheckRenderer::ActivateCell(const wxRect&, wxDataViewModel* model, const wxDataViewItem& item,
                                        unsigned int col, const wxMouseEvent*)
{
    if (model == nullptr)
    {
        return false;
    }

    /*
     * The renderer never saw a value of a row without a startup file: the
     * control does not render such cells at all, so the state is read back
     * from the model instead of trusting the last drawn row.
     */
    wxVariant value;
    model->GetValue(value, item, col);
    if (value.IsNull())
    {
        return false;
    }

    const wxVariant changed(!value.GetBool());
    return model->ChangeValue(changed, item, col);
}
