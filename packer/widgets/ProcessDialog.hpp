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
        int          compress;   /* Compress level. 0=no compress, 9=max compress. */
        std::wstring loaderPath; /* Loader path. */
        std::wstring outputPath; /* Output file path. */
        std::vector<FileDataView::Filesystem> filesystem; /* Filesystem input. */

        Config();
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Config, compress, loaderPath, outputPath,
                                                    filesystem);
    };

    ProcessDialog(wxWindow* parent, const appbox::Meta& meta, const Config& config);
    ~ProcessDialog() override;

public:
    struct Data;
    Data* m_data;
};

#endif
