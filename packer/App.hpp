#ifndef APPBOX_PACKER_APP_HPP
#define APPBOX_PACKER_APP_HPP

#include <wx/wx.h>

enum PackerWidgetID
{
    PACKER_LOWEST = wxID_HIGHEST + 1,
    PACKER_FILE_MENU_ADD_FOLDER_RECURSIVE,
    PACKER_FILE_MENU_DELETE,
};

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
