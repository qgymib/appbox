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
