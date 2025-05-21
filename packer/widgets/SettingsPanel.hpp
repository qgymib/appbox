#ifndef APPBOX_PACKER_WIDGETS_SETTINGS_PANEL_HPP
#define APPBOX_PACKER_WIDGETS_SETTINGS_PANEL_HPP

#include <wx/wx.h>

class SettingsPanel : public wxPanel
{
public:
    struct Config
    {
        std::wstring outputPath;
    };

public:
    SettingsPanel(wxWindow* parent);
    ~SettingsPanel();

    Config Export() const;

private:
    struct Data;
    Data* mData;
};

#endif
