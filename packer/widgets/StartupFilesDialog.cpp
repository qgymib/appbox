#include <wx/wx.h>
#include <wx/dataview.h>
#include "Common.hpp"
#include "StartupFilesDialog.hpp"

struct StartupFilesDataView : wxDataViewModel
{
    StartupFilesDataView(const appbox::MetaFileVec& config);
    ~StartupFilesDataView();
    void AddRecord();
    void DeleteRecord(wxDataViewItem& item);

    bool           IsContainer(const wxDataViewItem& item) const override;
    wxDataViewItem GetParent(const wxDataViewItem& item) const override;
    unsigned int   GetChildren(const wxDataViewItem& item,
                               wxDataViewItemArray&  children) const override;
    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    std::vector<appbox::MetaFile*> mFiles;
};

struct StartupFilesDialog::Data
{
    Data(StartupFilesDialog* owner, const appbox::MetaFileVec& config,
         const wxArrayString& choices);
    ~Data();
    void OnAddButtonClick(wxCommandEvent&);
    void OnDeleteButtonClick(wxCommandEvent&);

    StartupFilesDialog*                   mOwner;
    wxDataViewCtrl*                       mStartupFilesDataViewCtrl;
    wxObjectDataPtr<StartupFilesDataView> mStartupFilesDataView;
};

StartupFilesDataView::StartupFilesDataView(const appbox::MetaFileVec& config)
{
    for (auto it = config.begin(); it != config.end(); ++it)
    {
        appbox::MetaFile* copy = new appbox::MetaFile;
        *copy = *it;
        mFiles.push_back(copy);
    }
}

StartupFilesDataView::~StartupFilesDataView()
{
    for (auto it = mFiles.begin(); it != mFiles.end(); ++it)
    {
        delete *it;
    }
}

void StartupFilesDataView::AddRecord()
{
    appbox::MetaFile* file = new appbox::MetaFile;
    mFiles.push_back(file);
    ItemAdded(wxDataViewItem(nullptr), wxDataViewItem(file));
}

void StartupFilesDataView::DeleteRecord(wxDataViewItem& item)
{
    ItemDeleted(wxDataViewItem(nullptr), item);
    appbox::MetaFile* file = static_cast<appbox::MetaFile*>(item.GetID());
    for (auto it = mFiles.begin(); it != mFiles.end(); ++it)
    {
        if (*it == file)
        {
            mFiles.erase(it);
            delete file;
            return;
        }
    }
}

bool StartupFilesDataView::IsContainer(const wxDataViewItem& item) const
{
    return !item.IsOk();
}

wxDataViewItem StartupFilesDataView::GetParent(const wxDataViewItem&) const
{
    return wxDataViewItem(nullptr);
}

unsigned int StartupFilesDataView::GetChildren(const wxDataViewItem& item,
                                               wxDataViewItemArray&  children) const
{
    if (item.IsOk())
    {
        return 0;
    }

    for (auto it = mFiles.begin(); it != mFiles.end(); ++it)
    {
        appbox::MetaFile* file = *it;
        children.Add(wxDataViewItem(file));
    }

    return mFiles.size();
}

static void s_col_path_get(wxVariant& variant, const appbox::MetaFile* file)
{
    variant = file->path;
}

static bool s_col_path_set(const wxVariant& variant, appbox::MetaFile* file)
{
    file->path = variant.GetString().ToStdString(wxConvUTF8);
    return true;
}

static void s_col_cmd_get(wxVariant& variant, const appbox::MetaFile* file)
{
    variant = file->args;
}

static bool s_col_cmd_set(const wxVariant& variant, appbox::MetaFile* file)
{
    file->args = variant.GetString().ToStdString(wxConvUTF8);
    return true;
}

static void s_col_trigger_get(wxVariant& variant, const appbox::MetaFile* file)
{
    variant = file->trigger;
}

static bool s_col_trigger_set(const wxVariant& variant, appbox::MetaFile* file)
{
    file->trigger = variant.GetString().ToStdString(wxConvUTF8);
    return true;
}

static void s_col_autostart_get(wxVariant& variant, const appbox::MetaFile* file)
{
    variant = file->autoStart;
}

static bool s_col_autostart_set(const wxVariant& variant, appbox::MetaFile* file)
{
    file->autoStart = variant.GetBool();
    return true;
}

static DataViewValueGS<appbox::MetaFile> s_startup_files_data_view_gs[] = {
    { s_col_path_get,      s_col_path_set      },
    { s_col_cmd_get,       s_col_cmd_set       },
    { s_col_trigger_get,   s_col_trigger_set   },
    { s_col_autostart_get, s_col_autostart_set },
};

void StartupFilesDataView::GetValue(wxVariant& variant, const wxDataViewItem& item,
                                    unsigned int col) const
{
    wxASSERT(item.IsOk());
    appbox::MetaFile* file = static_cast<appbox::MetaFile*>(item.GetID());
    if (col >= std::size(s_startup_files_data_view_gs))
    {
        wxLogError("StartupFilesDataView::GetValue() called with invalid column index.");
        return;
    }
    s_startup_files_data_view_gs[col].GetValue(variant, file);
}

bool StartupFilesDataView::SetValue(const wxVariant& variant, const wxDataViewItem& item,
                                    unsigned int col)
{
    wxASSERT(item.IsOk());
    appbox::MetaFile* file = static_cast<appbox::MetaFile*>(item.GetID());
    if (col >= std::size(s_startup_files_data_view_gs))
    {
        wxLogError("StartupFilesDataView::SetValue() called with invalid column index.");
    }
    return s_startup_files_data_view_gs[col].SetValue(variant, file);
}

StartupFilesDialog::Data::Data(StartupFilesDialog* owner, const appbox::MetaFileVec& config,
                               const wxArrayString& choices)
{
    mOwner = owner;
    mStartupFilesDataView = new StartupFilesDataView(config);
    mStartupFilesDataViewCtrl = new wxDataViewCtrl(owner, wxID_ANY);
    mStartupFilesDataViewCtrl->AssociateModel(mStartupFilesDataView.get());

    {
        wxDataViewChoiceRenderer* c =
            new wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT);
        wxDataViewColumn* column = new wxDataViewColumn(_("Path"), c, 0, 2 * wxDVC_DEFAULT_WIDTH,
                                                        wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE);
        mStartupFilesDataViewCtrl->AppendColumn(column);
    }
    {
        wxDataViewTextRenderer* c = new wxDataViewTextRenderer(
            wxDataViewTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_EDITABLE);
        wxDataViewColumn* column =
            new wxDataViewColumn(_("Command Line"), c, 1, 2 * wxDVC_DEFAULT_WIDTH, wxALIGN_CENTER,
                                 wxDATAVIEW_COL_RESIZABLE);
        mStartupFilesDataViewCtrl->AppendColumn(column);
    }
    {
        wxDataViewTextRenderer* c = new wxDataViewTextRenderer(
            wxDataViewTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_EDITABLE);
        wxDataViewColumn* column = new wxDataViewColumn(_("Trigger"), c, 2, wxDVC_DEFAULT_WIDTH,
                                                        wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE);
        mStartupFilesDataViewCtrl->AppendColumn(column);
    }
    {
        wxDataViewToggleRenderer* c = new wxDataViewToggleRenderer(
            wxDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE);
        wxDataViewColumn* column = new wxDataViewColumn(
            _("Auto Start"), c, 3, wxDVC_TOGGLE_DEFAULT_WIDTH, wxALIGN_CENTER, 0);
        mStartupFilesDataViewCtrl->AppendColumn(column);
    }

    wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(new wxButton(owner, wxID_ADD));
        sizer->Add(new wxButton(owner, wxID_DELETE));
        bSizer->Add(sizer);
    }
    bSizer->Add(mStartupFilesDataViewCtrl, 1, wxEXPAND);
    bSizer->Add(owner->CreateSeparatedButtonSizer(wxOK | wxCANCEL));
    owner->SetSizer(bSizer);

    owner->Bind(wxEVT_BUTTON, &Data::OnAddButtonClick, this, wxID_ADD);
    owner->Bind(wxEVT_BUTTON, &Data::OnDeleteButtonClick, this, wxID_DELETE);
}

StartupFilesDialog::Data::~Data()
{
}

void StartupFilesDialog::Data::OnAddButtonClick(wxCommandEvent&)
{
    mStartupFilesDataView->AddRecord();
}

void StartupFilesDialog::Data::OnDeleteButtonClick(wxCommandEvent&)
{
    wxDataViewItem item = mStartupFilesDataViewCtrl->GetSelection();
    mStartupFilesDataView->DeleteRecord(item);
}

StartupFilesDialog::StartupFilesDialog(wxWindow* parent, const appbox::MetaFileVec& config,
                                       const wxArrayString& choices)
    : wxDialog(parent, wxID_ANY, _("Startup Files"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    mData = new Data(this, config, choices);
}

StartupFilesDialog::~StartupFilesDialog()
{
    delete mData;
}

appbox::MetaFileVec StartupFilesDialog::GetResult() const
{
    appbox::MetaFileVec result;
    for (auto it = mData->mStartupFilesDataView->mFiles.begin();
         it != mData->mStartupFilesDataView->mFiles.end(); ++it)
    {
        appbox::MetaFile* record = *it;
        result.push_back(*record);
    }
    return result;
}
