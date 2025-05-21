#ifndef APPBOX_LOADER_WIDGETS_LOG_PANEL_HPP
#define APPBOX_LOADER_WIDGETS_LOG_PANEL_HPP

#include <wx/wx.h>

class LogPanel : public wxPanel
{
public:
    LogPanel(wxWindow* parent);
    virtual ~LogPanel();

private:
    struct Data;
    Data* m_data;
};

#endif
