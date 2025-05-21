#include <wx/wx.h>
#include <wx/textdlg.h>
#include <wx/filename.h>
#include <spdlog/spdlog.h>
#include "FileDataView.hpp"
#include "FilePanel.hpp"

struct FilePanel::Data
{
    Data(FilePanel* owner);
    void OnDataViewContextMenu(wxDataViewEvent&);
    void OnMenuAddFolderRecursive(wxCommandEvent&);

    FilePanel*      mOwner;
    FileDataView*   mFileDataView;
    wxDataViewCtrl* mFileDataViewCtrl;
};

FilePanel::Data::Data(FilePanel* owner)
{
    mOwner = owner;
    mFileDataView = new FileDataView;

    mFileDataViewCtrl = new wxDataViewCtrl(owner, wxID_ANY);
    mFileDataViewCtrl->AssociateModel(mFileDataView);
    mFileDataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &Data::OnDataViewContextMenu, this);

    {
        wxDataViewTextRenderer* tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
        wxDataViewColumn*       column0 =
            new wxDataViewColumn("Name", tr, 0, 320, wxALIGN_CENTER,
                                 wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
        mFileDataViewCtrl->AppendColumn(column0);
    }
    {
        wxArrayString             choices = mFileDataView->GetIsolationArray();
        wxDataViewChoiceRenderer* c = new wxDataViewChoiceRenderer(
            choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
        wxDataViewColumn* column1 =
            new wxDataViewColumn("Isolation", c, 1, wxDVC_DEFAULT_WIDTH, wxALIGN_CENTER,
                                 wxDATAVIEW_COL_REORDERABLE | wxDATAVIEW_COL_RESIZABLE);
        mFileDataViewCtrl->AppendColumn(column1);
    }

    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(mFileDataViewCtrl, 1, wxGROW | wxALL);
    owner->SetSizerAndFit(sizer);
}

void FilePanel::Data::OnDataViewContextMenu(wxDataViewEvent& e)
{
    wxMenu menu;
    menu.Append(1, "Add Folder Recursive");
    menu.Bind(wxEVT_MENU, &Data::OnMenuAddFolderRecursive, this, 1, wxID_ANY,
              new wxDataViewEvent(e));
    mFileDataViewCtrl->PopupMenu(&menu);
}

void FilePanel::Data::OnMenuAddFolderRecursive(wxCommandEvent& e)
{
    wxDirDialog dirDlg(nullptr);
    if (dirDlg.ShowModal() != wxID_OK)
    {
        return;
    }

    std::wstring sourceDirPath = dirDlg.GetPath().ToStdWstring();
    spdlog::info(L"SourceDir: {}", sourceDirPath);

    wxTextEntryDialog textDialog(nullptr, _("Sandbox Path"));
    textDialog.SetValue(dirDlg.GetPath());
    if (textDialog.ShowModal() != wxID_OK)
    {
        return;
    }
    std::wstring sandboxDirPath = textDialog.GetValue().ToStdWstring();
    sandboxDirPath = wxFileName(sandboxDirPath).GetFullPath();
    spdlog::info(L"SandboxDir: {}", sandboxDirPath);

    wxDataViewEvent* orig_event = static_cast<wxDataViewEvent*>(e.GetEventUserData());
    mFileDataView->AddFolderRecursive(orig_event->GetItem(), sandboxDirPath, sourceDirPath);
}

FilePanel::FilePanel(wxWindow* parent) : wxPanel(parent)
{
    m_data = new Data(this);
}

FilePanel::~FilePanel()
{
    delete m_data;
}

std::vector<FileDataView::Filesystem> FilePanel::Export() const
{
    return m_data->mFileDataView->Export();
}
