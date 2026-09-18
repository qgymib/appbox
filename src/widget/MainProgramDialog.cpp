#include "MainProgramDialog.hpp"
#include "StartupCheckRenderer.hpp"

/**
 * @brief Create the main program browser.
 * @param[in] parent Parent window.
 * @param[in] model The pack model holding the imports.
 */
MainProgramDialog::MainProgramDialog(wxWindow* parent, const appbox::PackModel& model)
    : wxDialog(parent, wxID_ANY, "Select Main Program", wxDefaultPosition, wxSize(640, 520))
{
    tree_model_ = new StartupTreeModel(model);

    tree_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxDV_ROW_LINES | wxDV_SINGLE);
    tree_->AssociateModel(tree_model_);

    /* AssociateModel() adds a reference which the control releases again. */
    tree_model_->DecRef();

    wxDataViewColumn* const name_column =
        tree_->AppendIconTextColumn("Name", StartupTreeModel::NameColumn, wxDATAVIEW_CELL_INERT, 300,
                                    wxALIGN_LEFT);
    tree_->AppendTextColumn("Type", StartupTreeModel::TypeColumn, wxDATAVIEW_CELL_INERT, 100,
                            wxALIGN_LEFT);

    /*
     * The startup column uses a custom renderer: only the executable rows
     * report a value for it, so only they show a checkbox.
     */
    tree_->AppendColumn(new wxDataViewColumn("Startup", new StartupCheckRenderer,
                                             StartupTreeModel::StartupColumn, 90, wxALIGN_CENTER));
    tree_->SetExpanderColumn(name_column);

    ok_button_ = new wxButton(this, wxID_OK);
    auto* cancel_button = new wxButton(this, wxID_CANCEL);

    auto* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton(ok_button_);
    buttons->AddButton(cancel_button);
    buttons->Realize();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY,
                                "Browse the imported folders and tick the executable which starts the "
                                "sandboxed application:"),
               0, wxALL, 8);
    sizer->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sizer->Add(buttons, 0, wxEXPAND | wxBOTTOM | wxRIGHT, 8);

    SetSizer(sizer);

    tree_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &MainProgramDialog::OnItemActivated, this);
    tree_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &MainProgramDialog::OnValueChanged, this);
    Bind(wxEVT_BUTTON, &MainProgramDialog::OnOk, this, wxID_OK);

    if (model.HasMainProgram())
    {
        const auto choice = model.MainProgramChoice();
        tree_model_->Preselect(choice);
        RevealSelection(choice);
    }

    UpdateOkButton();
}

const appbox::MainProgram& MainProgramDialog::Selection() const
{
    return selection_;
}

void MainProgramDialog::RevealSelection(const appbox::MainProgram& choice)
{
    appbox::StartupNode* const node = tree_model_->FindChoice(choice);
    if (node == nullptr)
    {
        return;
    }

    const wxDataViewItem item = tree_model_->Item(node);

    /* Expand() opens the ancestors of the row as well. */
    tree_->Expand(item);
    tree_->EnsureVisible(item);
}

void MainProgramDialog::UpdateOkButton()
{
    ok_button_->Enable(tree_model_->HasChecked());
}

void MainProgramDialog::Accept()
{
    if (!tree_model_->HasChecked())
    {
        return;
    }

    selection_ = tree_model_->Checked();
    EndModal(wxID_OK);
}

void MainProgramDialog::OnItemActivated(wxDataViewEvent& event)
{
    const appbox::StartupNode* const node = tree_model_->Node(event.GetItem());
    if (node == nullptr || !appbox::StartupTree::IsCheckable(*node))
    {
        /* A folder keeps the default behaviour and expands instead. */
        event.Skip();
        return;
    }

    if (tree_model_->SetChecked(event.GetItem()))
    {
        Accept();
    }
}

void MainProgramDialog::OnValueChanged(wxDataViewEvent& event)
{
    UpdateOkButton();
    event.Skip();
}

void MainProgramDialog::OnOk(wxCommandEvent&)
{
    Accept();
}
