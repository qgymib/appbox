#include <wx/wx.h>
#include "WString.hpp"
#include "RegistryBrowser.hpp"
#include "MainFrame.hpp"

/**
 * @brief Create the sandbox registry browser window.
 * @param[in] registry_hive_path The UTF-8 DOS path of the sandbox registry
 *                               hive file which the browser shows.
 */
MainFrame::MainFrame(const std::string& registry_hive_path)
    : wxFrame(nullptr, wxID_ANY, "AppBox Loader", wxDefaultPosition, wxSize(960, 640))
{
    SetMinSize(wxSize(480, 320));

    /* The frame hosts the read-only browser of the sandbox registry hive. */
    new RegistryBrowser(this, appbox::UTF8ToWide(registry_hive_path));
}

MainFrame::~MainFrame()
{
}
