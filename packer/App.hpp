#ifndef APPBOX_PACKER_APP_HPP
#define APPBOX_PACKER_APP_HPP

#include <wx/wx.h>

class PackerApp : public wxApp
{
public:
    bool OnInit() override;
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    std::wstring   loaderPath;   /* Path to loader. */
    std::wstring   templatePath; /* Path to template file. */
    nlohmann::json templateJson; /* Build template. */
    bool           nogui;        /* Hide GUI. */
    bool           process;      /* Process directly. */
};

wxDECLARE_APP(PackerApp);

#endif
