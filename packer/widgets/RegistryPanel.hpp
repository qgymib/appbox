#ifndef APPBOX_PACKER_WIDGETS_REGISTRY_PANEL_HPP
#define APPBOX_PACKER_WIDGETS_REGISTRY_PANEL_HPP

#include <wx/wx.h>

class RegistryPanel : public wxPanel
{
public:
    RegistryPanel(wxWindow* parent);
    ~RegistryPanel();

private:
    struct Data;
    Data* m_data;
};

#endif
