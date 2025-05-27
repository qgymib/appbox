#ifndef APPBOX_PACKER_WIDGETS_STARTUP_FILES_DIALOG_HPP
#define APPBOX_PACKER_WIDGETS_STARTUP_FILES_DIALOG_HPP

#include <wx/wx.h>
#include <vector>
#include "utils/meta.hpp"

class StartupFilesDialog : public wxDialog
{
public:
    StartupFilesDialog(wxWindow* parent, const appbox::MetaFileVec& config, const wxArrayString& choices);
    ~StartupFilesDialog() override;

    appbox::MetaFileVec GetResult() const;

private:
    struct Data;
    Data* mData;
};

#endif
