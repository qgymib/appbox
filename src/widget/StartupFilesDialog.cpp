#include "StartupFilesDialog.hpp"
#include "StartupCheckRenderer.hpp"

StartupFilesDialog::StartupFilesDialog(wxWindow* parent, const appbox::PackModel& model)
    : wxDialog(parent, wxID_ANY, "Startup Files", wxDefaultPosition, wxSize(720, 540))
{
    tree_model_ = new StartupTreeModel(model);

    tree_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
    tree_->AssociateModel(tree_model_);

    /* AssociateModel() adds a reference which the control releases again. */
    tree_model_->DecRef();

    wxDataViewColumn* const name_column =
        tree_->AppendIconTextColumn("Name", StartupTreeModel::NameColumn, wxDATAVIEW_CELL_INERT, 320, wxALIGN_LEFT);
    tree_->AppendTextColumn("Type", StartupTreeModel::TypeColumn, wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT);

    /*
     * The auto start column uses a custom renderer: only the executable rows
     * report a value for it, so only they show a checkbox.
     */
    tree_->AppendColumn(new wxDataViewColumn("Auto Start", new StartupCheckRenderer, StartupTreeModel::AutoStartColumn,
                                             90, wxALIGN_CENTER));
    tree_->AppendTextColumn("Trigger", StartupTreeModel::TriggerColumn, wxDATAVIEW_CELL_EDITABLE, 160, wxALIGN_LEFT);
    tree_->SetExpanderColumn(name_column);

    ok_button_ = new wxButton(this, wxID_OK);
    auto* cancel_button = new wxButton(this, wxID_CANCEL);
    remove_button_ = new wxButton(this, wxID_ANY, "Remove");

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(remove_button_, 0, wxALIGN_CENTER_VERTICAL);
    buttons->AddStretchSpacer();
    buttons->Add(ok_button_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    buttons->Add(cancel_button, 0, wxALIGN_CENTER_VERTICAL);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY,
                                "Tick the executables the sandbox starts on its own. The trigger names the file "
                                "for the --X-AppBox-Startup option of the sandbox:"),
               0, wxALL, 8);
    sizer->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sizer->Add(buttons, 0, wxEXPAND | wxBOTTOM | wxRIGHT, 8);

    SetSizer(sizer);

    tree_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &StartupFilesDialog::OnItemActivated, this);
    tree_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &StartupFilesDialog::OnValueChanged, this);
    tree_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &StartupFilesDialog::OnSelectionChanged, this);
    Bind(wxEVT_BUTTON, &StartupFilesDialog::OnRemove, this, remove_button_->GetId());
    Bind(wxEVT_BUTTON, &StartupFilesDialog::OnOk, this, wxID_OK);

    tree_model_->Preselect(model.StartupFiles());
    RevealSelection(model.StartupFiles());

    UpdateButtons();
}

const std::vector<appbox::StartupFile>& StartupFilesDialog::Selection() const
{
    return selection_;
}

void StartupFilesDialog::RevealSelection(const std::vector<appbox::StartupFile>& files)
{
    for (const auto& file : files)
    {
        appbox::StartupNode* const node = tree_model_->FindChoice(file);
        if (node == nullptr)
        {
            continue;
        }

        const wxDataViewItem item = tree_model_->Item(node);

        /* Expand() opens the ancestors of the row as well. */
        tree_->Expand(item);
        tree_->EnsureVisible(item);
    }
}

void StartupFilesDialog::UpdateButtons()
{
    ok_button_->Enable(tree_model_->HasFiles());
    remove_button_->Enable(tree_model_->IsStartupFile(tree_->GetSelection()));
}

void StartupFilesDialog::Accept()
{
    if (!tree_model_->HasFiles())
    {
        return;
    }

    selection_ = tree_model_->Files();
    EndModal(wxID_OK);
}

void StartupFilesDialog::OnItemActivated(wxDataViewEvent& event)
{
    const appbox::StartupNode* const node = tree_model_->Node(event.GetItem());
    if (node == nullptr || !appbox::StartupTree::IsCheckable(*node))
    {
        /* A folder keeps the default behaviour and expands instead. */
        event.Skip();
        return;
    }

    tree_model_->ChangeValue(wxVariant(!tree_model_->IsAutoStart(event.GetItem())), event.GetItem(),
                             StartupTreeModel::AutoStartColumn);
    UpdateButtons();
}

void StartupFilesDialog::OnValueChanged(wxDataViewEvent& event)
{
    const wxString error = tree_model_->TakeError();
    if (!error.empty())
    {
        wxMessageBox(error, "Startup Files", wxOK | wxICON_ERROR, this);

        /*
         * The control kept the text the user typed: asking it to read the cell
         * again restores the value the model stored.
         */
        tree_model_->ValueChanged(event.GetItem(), event.GetColumn());
    }

    UpdateButtons();
    event.Skip();
}

void StartupFilesDialog::OnSelectionChanged(wxDataViewEvent& event)
{
    UpdateButtons();
    event.Skip();
}

void StartupFilesDialog::OnRemove(wxCommandEvent&)
{
    if (tree_model_->Remove(tree_->GetSelection()))
    {
        UpdateButtons();
    }
}

void StartupFilesDialog::OnOk(wxCommandEvent&)
{
    Accept();
}
