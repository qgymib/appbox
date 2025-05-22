#ifndef APPBOX_PACKER_WIDGETS_PROCESS_DIALOG_HPP
#define APPBOX_PACKER_WIDGETS_PROCESS_DIALOG_HPP

#include <wx/wx.h>
#include "utils/meta.hpp"
#include "FileDataView.hpp"

class ProcessDialog : public wxDialog
{
public:
    struct Config
    {
        int      compress;   /* Compress level. 0=no compress, 9=max compress. */
        wxString loaderPath; /* Loader path. */
        wxString outputPath; /* Output file path. */
        std::vector<FileDataView::Filesystem> filesystem; /* Filesystem input. */

        Config();
    };

    ProcessDialog(wxWindow* parent, const appbox::Meta& meta, const Config& config);
    ~ProcessDialog() override;

public:
    struct Data;
    Data* m_data;
};

#endif
