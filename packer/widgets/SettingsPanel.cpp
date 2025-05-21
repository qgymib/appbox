#include <wx/wx.h>
#include <wx/filename.h>
#include "SettingsPanel.hpp"

struct SettingsPanel::Data
{
    Data(SettingsPanel* owner);
    void OnOutputBrowseButtonClick(wxCommandEvent&);

    SettingsPanel* mOwner;
    wxTextCtrl*    mOutputTextCtrl;
    wxButton*      mOutputBrowseButton;
};

SettingsPanel::Data::Data(SettingsPanel* owner)
{
    mOwner = owner;

    wxFlexGridSizer* gSizer = new wxFlexGridSizer(3);
    {
        gSizer->Add(new wxStaticText(owner, wxID_ANY, _("Output")));
        mOutputTextCtrl = new wxTextCtrl(owner, wxID_ANY);
        gSizer->Add(mOutputTextCtrl, 1, wxGROW);
        mOutputBrowseButton = new wxButton(owner, wxID_ANY, _("Browse"));
        gSizer->Add(mOutputBrowseButton);
    }
    owner->SetSizerAndFit(gSizer);

    mOutputBrowseButton->Bind(wxEVT_BUTTON, &Data::OnOutputBrowseButtonClick, this);
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
    return config;
}
