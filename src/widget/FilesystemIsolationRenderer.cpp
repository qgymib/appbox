#include "FilesystemIsolationRenderer.hpp"
#include <wx/choice.h>
#include <utility>

FilesystemIsolationRenderer::FilesystemIsolationRenderer(const wxArrayString& choices,
                                                        ChoicesCallback row_choices)
    : wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE),
      row_choices_(std::move(row_choices))
{
}

wxWindow* FilesystemIsolationRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect,
                                                        const wxVariant& value)
{
    /*
     * The item which is being edited is known to the renderer base class: it
     * stores it before it asks for the editor control, so the options of the
     * row can be built here.
     */
    wxArrayString options;
    if (row_choices_ != nullptr)
    {
        options = row_choices_(m_item);
    }
    if (options.IsEmpty())
    {
        /* Without an answer the options of the column keep the cell editable. */
        options = GetChoices();
    }

    auto* choice = new wxChoice(parent, wxID_ANY, labelRect.GetTopLeft(),
                                wxSize(labelRect.GetWidth(), -1), options);
    choice->Move(labelRect.GetRight() - choice->GetRect().width, wxDefaultCoord);
    choice->SetStringSelection(value.GetString());
    return choice;
}
