#include <wx/wx.h>
#include <wx/icon.h>
#include "WString.hpp"
#include "RegistryBrowser.hpp"
#include "MainFrame.hpp"

namespace
{

/**
 * @brief Name of the icon resource embedded by resource.rc.
 *
 * The resource compiler stores resource names in upper case and the lookup is
 * case insensitive, so the name matches the resource whatever the spelling in
 * the resource script is.
 */
constexpr const char* kWindowIconResource = "IDI_ICON1";

} // namespace

/**
 * @brief Create the sandbox registry browser window.
 * @param[in] registry_hive_path The UTF-8 DOS path of the sandbox registry
 *                               hive file which the browser shows.
 */
MainFrame::MainFrame(const std::string& registry_hive_path)
    : wxFrame(nullptr, wxID_ANY, "AppBox Loader", wxDefaultPosition, wxSize(960, 640))
{
    SetMinSize(wxSize(480, 320));

    ApplyWindowIcon();

    /* The frame hosts the read-only browser of the sandbox registry hive. */
    new RegistryBrowser(this, appbox::UTF8ToWide(registry_hive_path));
}

MainFrame::~MainFrame()
{
}

void MainFrame::ApplyWindowIcon()
{
    /*
     * A packed loader also carries the icon of the packaged application, which
     * the packer appends for Explorer. The window icon is set explicitly so
     * the title bar, the taskbar button and the Alt-Tab list keep the icon of
     * the loader and do not fall back to the first icon group of the file.
     */
    const wxIcon icon(kWindowIconResource, wxBITMAP_TYPE_ICO_RESOURCE);
    if (icon.IsOk())
    {
        SetIcon(icon);
    }
}
