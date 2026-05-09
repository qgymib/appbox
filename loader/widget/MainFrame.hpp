#ifndef APPBOX_LOADER_WIDGET_MAIN_FRAME_HPP
#define APPBOX_LOADER_WIDGET_MAIN_FRAME_HPP

#include <wx/wx.h>

class MainFrame : public wxFrame
{
public:
    MainFrame();
    virtual ~MainFrame();

    struct Data;
private:
    Data* data_;
};

#endif
