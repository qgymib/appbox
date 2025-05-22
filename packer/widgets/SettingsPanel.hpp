#ifndef APPBOX_PACKER_WIDGETS_SETTINGS_PANEL_HPP
#define APPBOX_PACKER_WIDGETS_SETTINGS_PANEL_HPP

#include <wx/wx.h>
#include <nlohmann/json.hpp>

class SettingsPanel : public wxPanel
{
public:
    struct Config
    {
        int         compressLevel; /* Compress level. */
        std::string outputPath;    /* The path of the output file. */
        std::string sandboxPath;   /* The path of the sandbox environment. */

        Config();
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Config, compressLevel, outputPath, sandboxPath);
    };

public:
    SettingsPanel(wxWindow* parent);
    ~SettingsPanel();

    Config Export() const;
    void Import(const Config& config);

private:
    struct Data;
    Data* mData;
};

#endif
