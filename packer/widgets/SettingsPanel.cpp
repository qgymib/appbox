#include <wx/wx.h>
#include <wx/filename.h>
#include "SettingsPanel.hpp"

struct SettingsPanel::Data
{
    Data(SettingsPanel* owner);
    void OnOutputBrowseButtonClick(wxCommandEvent&);
    void OnSandboxBrowseButtonClick(wxCommandEvent&);

    SettingsPanel* mOwner;
    wxTextCtrl*    mOutputTextCtrl;
    wxButton*      mOutputBrowseButton;
    wxTextCtrl*    mSandboxTextCtrl;
    wxButton*      mSandboxBrowseButton;
};

SettingsPanel::Data::Data(SettingsPanel* owner)
{
    mOwner = owner;

    wxFlexGridSizer* gSizer = new wxFlexGridSizer(3, wxSize(5, 5));
    gSizer->AddGrowableCol(1);
    {
        gSizer->Add(new wxStaticText(owner, wxID_ANY, _("Output")));
        mOutputTextCtrl = new wxTextCtrl(owner, wxID_ANY);
        gSizer->Add(mOutputTextCtrl, 1, wxGROW);
        mOutputBrowseButton = new wxButton(owner, wxID_ANY, _("Browse"));
        gSizer->Add(mOutputBrowseButton);
    }
    {
        gSizer->Add(new wxStaticText(owner, wxID_ANY, _("Sandbox location")));
        mSandboxTextCtrl =
            new wxTextCtrl(owner, wxID_ANY, L"%LocalAppData%\\AppBox\\Sandbox\\%APPBOX::ENV::TITLE%");
        gSizer->Add(mSandboxTextCtrl, 1, wxGROW);
        mSandboxBrowseButton = new wxButton(owner, wxID_ANY, _("Browse"));
        gSizer->Add(mSandboxBrowseButton);
    }
    owner->SetSizer(gSizer);

    mOutputBrowseButton->Bind(wxEVT_BUTTON, &Data::OnOutputBrowseButtonClick, this);
    mSandboxBrowseButton->Bind(wxEVT_BUTTON, &Data::OnSandboxBrowseButtonClick, this);
}

void SettingsPanel::Data::OnOutputBrowseButtonClick(wxCommandEvent&)
{
    wxFileDialog fileDlg(mOwner, _("Save"), wxEmptyString, wxEmptyString,
                         L"Executable file (*.exe)|*.exe", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (fileDlg.ShowModal() != wxID_OK)
    {
        return;
    }

    mOutputTextCtrl->SetValue(fileDlg.GetPath());
}

void SettingsPanel::Data::OnSandboxBrowseButtonClick(wxCommandEvent&)
{
    wxDirDialog dirDlg(mOwner, _("Sandbox location"));
    if (dirDlg.ShowModal() != wxID_OK)
    {
        return;
    }

    mSandboxTextCtrl->SetValue(dirDlg.GetPath());
}

SettingsPanel::SettingsPanel(wxWindow* parent) : wxPanel(parent)
{
    mData = new Data(this);
}

SettingsPanel::~SettingsPanel()
{
    delete mData;
}

SettingsPanel::Config SettingsPanel::Export() const
{
    SettingsPanel::Config config;
    config.outputPath = wxFileName(mData->mOutputTextCtrl->GetValue()).GetFullPath();
    config.sandboxPath = wxFileName(mData->mSandboxTextCtrl->GetValue()).GetFullPath();
    return config;
}
