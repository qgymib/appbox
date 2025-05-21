#ifndef APPBOX_PACKER_WIDGETS_FILE_PANEL_HPP
#define APPBOX_PACKER_WIDGETS_FILE_PANEL_HPP

#include <wx/wx.h>
#include "FileDataView.hpp"

class FilePanel : public wxPanel
{
public:
    FilePanel(wxWindow* parent);
    virtual ~FilePanel();

    std::vector<FileDataView::Filesystem> Export() const;

private:
    struct Data;
    Data* m_data;
};

#endif
