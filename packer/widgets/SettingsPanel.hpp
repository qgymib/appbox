#ifndef APPBOX_PACKER_WIDGETS_SETTINGS_PANEL_HPP
#define APPBOX_PACKER_WIDGETS_SETTINGS_PANEL_HPP

#include <wx/wx.h>

class SettingsPanel : public wxPanel
{
public:
    struct Config
    {
        int          compressLevel; /* Compress level. */
        std::wstring outputPath;    /* The path of the output file. */
        std::wstring sandboxPath;   /* The path of the sandbox environment. */
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
