/*
 * The header chain of this file reaches `<windows.h>` through
 * `common/ErrorCode.hpp`, and `winnt.h` defines aliases of the `STATUS_*`
 * names which `ntstatus.h` defines. A translation unit which spells such a
 * name therefore takes the table of `ntstatus.h`, which `common/WinAPI.h`
 * is the first header of when it is used; the two lines below give the same
 * precedence without the rest of that header.
 */
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <wx/wx.h>
#include "widget/MainFrame.hpp"

/**
 * @brief Application object of the packer.
 */
class AppBoxApp : public wxApp
{
public:
    /**
     * @brief Create and show the main frame.
     * @return true when the application should run.
     */
    bool OnInit() override;
};

bool AppBoxApp::OnInit()
{
    auto* frame = new MainFrame();
    frame->Show(true);
    return true;
}

wxIMPLEMENT_APP(AppBoxApp); // NOLINT
