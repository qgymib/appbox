#ifndef APPBOX_PACKER_WIDGETS_MAINFRAME_HPP
#define APPBOX_PACKER_WIDGETS_MAINFRAME_HPP

#include <wx/wx.h>

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame() override;

private:
    struct Data;
    Data* m_data;
};

#endif
