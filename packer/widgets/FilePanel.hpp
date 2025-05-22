#ifndef APPBOX_PACKER_WIDGETS_FILE_PANEL_HPP
#define APPBOX_PACKER_WIDGETS_FILE_PANEL_HPP

#include <wx/wx.h>
#include "FileDataView.hpp"

class FilePanel : public wxPanel
{
public:
    struct Config
    {
        std::vector<FileDataView::Filesystem> fs;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Config, fs);
    };

    FilePanel(wxWindow* parent);
    virtual ~FilePanel();

    Config Export() const;
    void Import(const Config& config);

private:
    struct Data;
    Data* m_data;
};

#endif
