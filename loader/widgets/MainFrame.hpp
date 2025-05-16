#ifndef APBOX_LOADER_WIDGETS_MAINFRAME_HPP
#define APBOX_LOADER_WIDGETS_MAINFRAME_HPP

#include <wx/wx.h>

class MainFrame : public wxFrame
{
public:
    MainFrame();
    virtual ~MainFrame();

private:
    struct Data;
    Data* m_data;
};

#endif
