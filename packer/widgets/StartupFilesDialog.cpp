#include <wx/wx.h>
#include <wx/dataview.h>
#include "StartupFilesDialog.hpp"

struct StartupFilesDataView : wxDataViewModel
{
    StartupFilesDataView(const StartupFilesDialog::Config& config);
    ~StartupFilesDataView();
    void AddRecord();
    void DeleteRecord(wxDataViewItem& item);

    bool           IsContainer(const wxDataViewItem& item) const override;
    wxDataViewItem GetParent(const wxDataViewItem& item) const override;
    unsigned int   GetChildren(const wxDataViewItem& item,
                               wxDataViewItemArray&  children) const override;
    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    std::vector<StartupFilesDialog::File*> mFiles;
};

struct StartupFilesValueGS
{
    void (*GetValue)(wxVariant& variant, const StartupFilesDialog::File* file);
    bool (*SetValue)(const wxVariant& variant, StartupFilesDialog::File* file);
};

struct StartupFilesDialog::Data
{
    Data(StartupFilesDialog* owner, const Config& config);
    ~Data();
    void OnAddButtonClick(wxCommandEvent&);
    void OnDeleteButtonClick(wxCommandEvent&);

    StartupFilesDialog*   mOwner;
    wxDataViewCtrl*       mStartupFilesDataViewCtrl;
    StartupFilesDataView* mStartupFilesDataView;
};

StartupFilesDataView::StartupFilesDataView(const StartupFilesDialog::Config& config)
{
    for (auto it = config.files.begin(); it != config.files.end(); ++it)
    {
        StartupFilesDialog::File* copy = new StartupFilesDialog::File;
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
    StartupFilesDialog::File* file = new StartupFilesDialog::File;
    mFiles.push_back(file);
    ItemAdded(wxDataViewItem(nullptr), wxDataViewItem(file));
}

void StartupFilesDataView::DeleteRecord(wxDataViewItem& item)
{
    ItemDeleted(wxDataViewItem(nullptr), item);
    StartupFilesDialog::File* file = static_cast<StartupFilesDialog::File*>(item.GetID());
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

bool StartupFilesDataView::IsContainer(const wxDataViewItem&) const
{
    return false;
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
        StartupFilesDialog::File* file = *it;
        children.Add(wxDataViewItem(file));
    }

    return mFiles.size();
}

static void s_col_path_get(wxVariant& variant, const StartupFilesDialog::File* file)
{
    variant = file->path;
}

static bool s_col_path_set(const wxVariant& variant, StartupFilesDialog::File* file)
{
    file->path = variant.GetString();
    return true;
}

static StartupFilesValueGS s_startup_files_data_view_gs[] = {
    { s_col_path_get, s_col_path_set },
};

static wxArrayString s_get_all_exes_from_config(const StartupFilesDialog::Config& config)
{
    wxArrayString result;
    for (auto it = config.files.begin(); it != config.files.end(); ++it)
    {
        const StartupFilesDialog::File& file = *it;
        const wxString                  path = wxString::FromUTF8(file.path);
        if (path.EndsWith(L".exe", nullptr))
        {
            result.Add(path);
        }
    }
    return result;
}

void StartupFilesDataView::GetValue(wxVariant& variant, const wxDataViewItem& item,
                                    unsigned int col) const
{
    wxASSERT(item.IsOk());
    StartupFilesDialog::File* file = static_cast<StartupFilesDialog::File*>(item.GetID());
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
    StartupFilesDialog::File* file = static_cast<StartupFilesDialog::File*>(item.GetID());
    if (col >= std::size(s_startup_files_data_view_gs))
    {
        wxLogError("StartupFilesDataView::SetValue() called with invalid column index.");
    }
    return s_startup_files_data_view_gs[col].SetValue(variant, file);
}

StartupFilesDialog::Data::Data(StartupFilesDialog* owner, const Config& config)
{
    mOwner = owner;
    mStartupFilesDataView = new StartupFilesDataView(config);
    mStartupFilesDataViewCtrl = new wxDataViewCtrl(owner, wxID_ANY);
    mStartupFilesDataViewCtrl->AssociateModel(mStartupFilesDataView);

    {
        wxArrayString             choices = s_get_all_exes_from_config(config);
        wxDataViewChoiceRenderer* c =
            new wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT);
        wxDataViewColumn* column =
            new wxDataViewColumn(_("Path"), c, 0, wxDVC_DEFAULT_WIDTH, wxALIGN_CENTER,
                                 wxDATAVIEW_COL_REORDERABLE | wxDATAVIEW_COL_RESIZABLE);
        mStartupFilesDataViewCtrl->AppendColumn(column);
    }

    wxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
    bSizer->Add(mStartupFilesDataViewCtrl, 1, wxEXPAND);
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(new wxButton(owner, wxID_ADD));
        sizer->Add(new wxButton(owner, wxID_DELETE));
        bSizer->Add(sizer);
    }
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

StartupFilesDialog::StartupFilesDialog(wxWindow* parent, const Config& config)
    : wxDialog(parent, wxID_ANY, _("Startup Files"))
{
    mData = new Data(this, config);
}

StartupFilesDialog::~StartupFilesDialog()
{
    delete mData;
}

StartupFilesDialog::Config StartupFilesDialog::GetResult() const
{
    Config result;
    for (auto it = mData->mStartupFilesDataView->mFiles.begin();
         it != mData->mStartupFilesDataView->mFiles.end(); ++it)
    {
        StartupFilesDialog::File* record = *it;
        result.files.push_back(*record);
    }
    return result;
}
