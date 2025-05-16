#ifndef APPBOX_LOADER_APP_HPP
#define APPBOX_LOADER_APP_HPP

#include <wx/wx.h>

class LoaderApp : public wxApp // NOLINT
{
public:
    bool OnInit() override;
    int  OnExit() override;
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    struct Data;
    Data* m_data; // NOLINT
};
wxDECLARE_APP(LoaderApp);

/**
 * @brief Exit the application if no gui window shown.
 */
wxDECLARE_EVENT(APPBOX_EXIT_APPLICATION_NO_GUI, wxCommandEvent);

#endif
