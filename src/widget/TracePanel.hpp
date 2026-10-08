#ifndef APPBOX_PACKER_WIDGET_TRACE_PANEL_HPP
#define APPBOX_PACKER_WIDGET_TRACE_PANEL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/dataview.h>
#include "core/TracerModel.hpp"
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

class TracerListModel;

wxDECLARE_EVENT(APPBOX_TRACER_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(APPBOX_TRACER_FINISHED, wxThreadEvent);

/**
 * @brief `Trace` tab of the Debug workspace.
 *
 * The tab runs a program under `cdb.exe` and shows which of the functions of a view the
 * program used. The box above the list picks the target program and its arguments, the second
 * box the view: `Isolation entry points` (the default, the built in table of the entry points
 * of the three isolation domains) or `All exports` (every executable export parsed from the
 * traced DLLs). The rows of the list are grey while they are unused and black once a run
 * reported them; the used rows come first, both groups sorted ASCII ascending.
 *
 * The run itself executes on a background thread and reports back through thread events, the
 * way the pack run of the frame does. The `Run` button becomes `Stop` while a run is going
 * on, which asks the session to stop.
 */
class TracePanel : public wxPanel
{
public:
    /**
     * @brief Create the page.
     * @param[in] parent Parent window.
     */
    explicit TracePanel(wxWindow* parent);

    /**
     * @brief Stop a still running trace and join its thread.
     */
    ~TracePanel() override;

private:
    void OnBrowse(wxCommandEvent& event);       ///< Pick the target program.
    void OnTargetChanged(wxEvent& event);       ///< Rebuild the view after the target box was edited.
    void OnViewChanged(wxCommandEvent& event);  ///< Rebuild the view after the view box changed.
    void OnRun(wxCommandEvent& event);          ///< Start a run, or stop the running one.
    void OnExport(wxCommandEvent& event);       ///< Write the result as JSON.
    void OnTraceProgress(wxThreadEvent& event); ///< Show a progress line of the run.
    void OnTraceFinished(wxThreadEvent& event); ///< Mark the used rows and finish the run.

    /**
     * @brief Rebuild the list from the target and the view of the workspace.
     */
    void ReloadView();

    /**
     * @brief Switch the controls between the idle and the running state.
     * @param[in] running Whether a run is going on.
     */
    void SetRunning(bool running);

    /** Update the enabled state of the buttons. */
    void UpdateButtons();

    /**
     * @brief Show one line in the status line of the workspace.
     * @param[in] text Text to show.
     */
    void SetStatus(const wxString& text);

    /** @return The view the box shows. */
    appbox::TracerView CurrentView() const;

    wxTextCtrl*      target_ = nullptr;    ///< Target program box.
    wxTextCtrl*      arguments_ = nullptr; ///< Arguments of the target.
    wxComboBox*      view_ = nullptr;      ///< View box.
    wxButton*        browse_ = nullptr;    ///< `Browse...` button.
    wxButton*        run_ = nullptr;       ///< `Run` / `Stop` button.
    wxButton*        export_ = nullptr;    ///< `Export JSON...` button.
    wxStaticText*    status_ = nullptr;    ///< Status line.
    wxDataViewCtrl*  list_ = nullptr;      ///< The list of the view.
    TracerListModel* model_ = nullptr;     ///< Rows the list shows.

    std::vector<appbox::TracerEntry> entries_;            ///< Rows of the current view.
    appbox::tracer::TraceResult      result_;             ///< Result of the last run of the view.
    std::wstring                     target_path_;        ///< Resolved target of the view.
    std::wstring                     loaded_target_;      ///< Target the view was built for.
    std::filesystem::path            module_directory_;   ///< System modules of the target.
    bool                             has_result_ = false; ///< Whether `result_` describes a run.
    std::thread                      thread_;             ///< Worker of the running trace.
};

#endif // APPBOX_PACKER_WIDGET_TRACE_PANEL_HPP
