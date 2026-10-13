#include "StartupFilesDialog.hpp"
#include "StartupCheckRenderer.hpp"
#include "core/StartupTree.hpp"
#include <memory>

wxDEFINE_EVENT(APPBOX_STARTUP_SCAN_FINISHED, wxThreadEvent);

StartupFilesDialog::StartupFilesDialog(wxWindow* parent, const appbox::PackModel& model)
    : wxDialog(parent, wxID_ANY, "Startup Files", wxDefaultPosition, wxSize(720, 540))
{
    tree_model_ = new StartupTreeModel();

    tree_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
    tree_->AssociateModel(tree_model_);

    /* AssociateModel() adds a reference which the control releases again. */
    tree_model_->DecRef();

    wxDataViewColumn* const name_column =
        tree_->AppendIconTextColumn("Name", StartupTreeModel::NameColumn, wxDATAVIEW_CELL_INERT, 320, wxALIGN_LEFT);
    tree_->AppendTextColumn("Type", StartupTreeModel::TypeColumn, wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT);

    /*
     * The auto start column uses a custom renderer: only the rows which name a
     * startable file report a value for it, so only they show a checkbox.
     */
    tree_->AppendColumn(new wxDataViewColumn("Auto Start", new StartupCheckRenderer, StartupTreeModel::AutoStartColumn,
                                             90, wxALIGN_CENTER));
    tree_->AppendTextColumn("Trigger", StartupTreeModel::TriggerColumn, wxDATAVIEW_CELL_EDITABLE, 160, wxALIGN_LEFT);
    tree_->SetExpanderColumn(name_column);

    status_ = new wxStaticText(this, wxID_ANY, "Scanning the folders of the sandbox view...");

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
                                "Tick the files the sandbox starts on its own. The trigger names the file "
                                "for the --X-AppBox-Startup option of the sandbox:"),
               0, wxALL, 8);
    sizer->Add(status_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sizer->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sizer->Add(buttons, 0, wxEXPAND | wxBOTTOM | wxRIGHT, 8);

    SetSizer(sizer);

    tree_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &StartupFilesDialog::OnItemActivated, this);
    tree_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &StartupFilesDialog::OnValueChanged, this);
    tree_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &StartupFilesDialog::OnSelectionChanged, this);
    Bind(wxEVT_BUTTON, &StartupFilesDialog::OnRemove, this, remove_button_->GetId());
    Bind(wxEVT_BUTTON, &StartupFilesDialog::OnOk, this, wxID_OK);
    Bind(APPBOX_STARTUP_SCAN_FINISHED, &StartupFilesDialog::OnScanFinished, this);

    UpdateButtons();

    /*
     * The tree reads the host folders of the imported folders and keeps the
     * folders on the way to a startable file only, which is why it is built in
     * the background: the dialog opens at once and reports that it is scanning
     * instead of freezing until the host was read.
     *
     * The thread works on a copy of the model, so the caller stays free to
     * change the model it handed over while the scan runs.
     */
    const appbox::PackModel snapshot = model;
    scan_thread_ = std::thread([this, snapshot]() {
        auto tree = std::make_shared<appbox::StartupTree>(snapshot);
        if (scan_cancelled_.load())
        {
            /* The dialog is gone, so its tree is of no use to anybody. */
            return;
        }

        auto* event = new wxThreadEvent(APPBOX_STARTUP_SCAN_FINISHED);
        event->SetPayload(tree);
        this->GetEventHandler()->QueueEvent(event);
    });
}

StartupFilesDialog::~StartupFilesDialog()
{
    /*
     * The scan reports its result to this window, so it has to be joined before
     * the window is destroyed: the join makes sure that the thread does not
     * queue an event any more, and the pending events of the window are dropped
     * when its event handler is destroyed below.
     */
    scan_cancelled_ = true;
    if (scan_thread_.joinable())
    {
        scan_thread_.join();
    }
}

const std::vector<appbox::StartupFile>& StartupFilesDialog::Selection() const
{
    return selection_;
}

void StartupFilesDialog::OnScanFinished(wxThreadEvent& event)
{
    if (scan_thread_.joinable())
    {
        scan_thread_.join();
    }

    tree_model_->Adopt(event.GetPayload<std::shared_ptr<appbox::StartupTree>>());

    /* The dialog opens with the whole tree in view. */
    ExpandAll();

    status_->Hide();
    UpdateButtons();
    Layout();
}

void StartupFilesDialog::ExpandAll()
{
    /*
     * The control expands a row at a time, which would redraw the whole tree
     * for every one of them, so it is frozen until every row is open.
     */
    tree_->Freeze();
    for (const appbox::StartupNode* const node : tree_model_->ExpandableRows())
    {
        tree_->Expand(tree_model_->Item(node));
    }
    tree_->Thaw();

    /*
     * The startup files are the reason the dialog exists, so the first of them
     * is scrolled into view although the tree above it is expanded as well.
     */
    const std::vector<appbox::StartupFile>& files = tree_model_->Files();
    if (files.empty())
    {
        return;
    }

    const appbox::StartupNode* const node = tree_model_->FindChoice(files.front());
    if (node != nullptr)
    {
        tree_->EnsureVisible(tree_model_->Item(node));
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
