#ifndef APPBOX_PACKER_WIDGET_CAPTURE_WIZARD_HPP
#define APPBOX_PACKER_WIDGET_CAPTURE_WIZARD_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/treebase.h>
#include "ReportTreeCtrl.hpp"
#include "core/CaptureWizardModel.hpp"
#include "core/SnapshotApply.hpp"
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>

class ModeChoiceGroup;
class wxButton;
class wxCheckBox;
class wxCloseEvent;
class wxCommandEvent;
class wxFlexGridSizer;
class wxGauge;
class wxListBox;
class wxNotebook;
class wxPanel;
class wxSimplebook;
class wxStaticText;
class wxTextCtrl;
class wxTimer;
class wxTimerEvent;
class wxWindowDisabler;

/**
 * @brief Dialog which walks the steps of a capture, the way a setup wizard does.
 *
 * The dialog is the entry point of both captures of the packer: the capture
 * which records the changes of an installation while it runs and the capture
 * which reads the state of the machine before and after it and compares the two
 * reads. The left side is the tree of the steps of both captures with the one
 * the dialog shows marked in bold. The tree is a report and not a control: no
 * item can be picked, no branch can be collapsed and the tree cannot take the
 * focus, so a click of the user cannot move the mark away from the step the
 * dialog shows, see `ReportTreeCtrl`. The right side carries what the dialog
 * says about that step above the page of the step itself, and the bottom
 * carries the `Cancel` button, which is offered on every step, and the button
 * which leaves the step.
 *
 * The steps a mode walks are the ones `appbox::CaptureStages()` names for it, so
 * the tree, the pages and the buttons follow the mode alone:
 *
 *  1. the choice of the mode, which explains both captures below the option of
 *     each one and lists the prerequisites of the realtime capture, with the
 *     restart as an administrator, below the realtime option alone;
 *  2. the steps of the realtime capture: the recording, which shows the process
 *     and the entry the capture saw last and the number of the changes it
 *     recorded and which waits for the user to confirm that the installation is
 *     over, and the review;
 *  3. the steps of the snapshot capture: the source of the baseline, which the
 *     user picks between a state of the machine which is read now and the state
 *     a snapshot file an earlier run wrote, the file of the baseline, which
 *     names the file the baseline is kept in, the baseline, which asks for the
 *     work of the source that was picked and which shows the step of the file
 *     again when the run produces no baseline, the installation, which waits
 *     for the same confirmation, the comparison, which reads the state of the
 *     machine again and compares it with the baseline, and the review, which is
 *     the one the realtime capture ends in as well.
 *
 * The step of the analysis and the fill is the one both capture modes end in:
 * the dialog shows what the capture does while it analyzes the difference,
 * repairs the entries and writes them into the workspaces of the session, and
 * it lists the entries which could not be written. The work itself belongs to
 * the owner, which reports it into the dialog; the button which confirms the
 * capture is enabled once that work is over and the button which drops the
 * capture is greyed while the workspaces are written, because a capture which
 * reached that point cannot be taken back.
 *
 * The rules of the steps live in `appbox::CaptureWizardModel`, so the dialog
 * itself only renders them and forwards the clicks of the user. The work which
 * belongs to a step is not done by the dialog: the owner is told that a step was
 * entered or left, that the source of the baseline was picked and that the
 * capture was cancelled, and it reports the state of the work of the step into
 * the dialog.
 *
 * While the dialog is shown, every other window of the application is disabled,
 * so the user cannot work around the capture it reports. A dialog which the user
 * cancelled or confirmed hides itself and reports that through WasCancelled()
 * and WasConfirmed(); the owner destroys it, which is what keeps the dialog from
 * being destroyed inside one of its own event handlers.
 */
class CaptureWizard : public wxDialog
{
public:
    /**
     * @brief What the dialog tells its owner when it enters or leaves a step.
     *
     * The owner starts the recording when the wizard enters the step of the
     * recording and the comparison when it enters the step of the comparison,
     * and it ends the recording when the wizard enters the review.
     */
    using StageHandler = std::function<void(appbox::CaptureWizardStage from, appbox::CaptureWizardStage to)>;

    /**
     * @brief What the dialog asks its owner when the user browses for the file
     *        of the baseline of a snapshot capture.
     *
     * The handler shows the file dialog of the source which is picked and
     * answers with the path the user chose. It returns false when the user
     * dismissed the dialog, which leaves the path the user set as it is. The
     * dialog itself never reads or writes the file: it only names it.
     */
    using BrowseHandler = std::function<bool(appbox::SnapshotBaselineSource source, std::wstring& path)>;

    /**
     * @brief What the dialog asks its owner when it enters the step of the
     *        baseline.
     *
     * The handler reads the state of the machine into the file the user named
     * or accepts the state that file holds, and it ends by telling the dialog
     * the result through SetBaselineReady(): every request is answered, because
     * the step of the baseline cannot be left before the answer arrives, and an
     * answer which says that the run produced no baseline shows the step which
     * names the file again.
     */
    using BaselineHandler = std::function<void(appbox::SnapshotBaselineSource source, const std::wstring& path)>;

    /**
     * @brief Create the dialog.
     *
     * The dialog fits itself from the parts which never change and locks that
     * size, so the pages of the steps do not move the buttons around when the
     * user walks through them.
     *
     * @param[in] parent Window the dialog belongs to.
     * @param[in] title Title of the dialog.
     * @param[in] requirements Prerequisites of the realtime capture.
     */
    CaptureWizard(wxWindow* parent, const wxString& title, const appbox::CaptureRequirements& requirements);

    /**
     * @brief Stop the timer of the dialog and give the windows back.
     */
    ~CaptureWizard() override;

    /**
     * @brief Show or hide the dialog.
     * @param[in] show Whether the dialog is shown.
     * @return Whether the state changed.
     */
    bool Show(bool show = true) override;

    /**
     * @brief Set what the dialog tells its owner when it walks the steps.
     * @param[in] handler The handler.
     */
    void SetStageHandler(StageHandler handler);

    /**
     * @brief Set what the dialog asks its owner when the baseline is picked.
     * @param[in] handler The handler.
     */
    void SetBaselineHandler(BaselineHandler handler);

    /**
     * @brief Set what the dialog asks its owner when the user browses for the
     *        file of the baseline.
     * @param[in] handler The handler.
     */
    void SetBrowseHandler(BrowseHandler handler);

    /**
     * @brief Show what the work of one step does right now.
     *
     * The report belongs to a step and is dropped when the wizard left it, so a
     * late report of a run which is over cannot touch the page of another step.
     *
     * @param[in] stage The step which reports its work.
     * @param[in] action Description of the work.
     * @param[in] object The entry the capture is working on, may be empty.
     * @param[in] entries Text of the entries row, may be empty.
     */
    void SetProgressStatus(appbox::CaptureWizardStage stage, const wxString& action, const wxString& object,
                           const wxString& entries);

    /**
     * @brief Show what the analysis of the changes does right now.
     *
     * The text comes from the owner and can be of any length, so it is wrapped
     * to the width of the page and the page of the review is laid out again: a
     * text which needs more lines than the one before it would otherwise run
     * over the rows below it.
     *
     * @param[in] text Description of the work of the analysis.
     */
    void SetAnalysisStatus(const wxString& text);

    /**
     * @brief Tell the dialog how the work of the baseline ended.
     *
     * The call is the answer to the request the dialog made through its
     * baseline handler: it opens the step which follows when the baseline is
     * ready, and it shows the step which names the file again when the run
     * produced no baseline, so the path can be fixed and the work asked for
     * again.
     *
     * @param[in] ready Whether the baseline is ready.
     */
    void SetBaselineReady(bool ready);

    /**
     * @brief Tell the dialog that the comparison of a snapshot capture failed.
     *
     * The step of the comparison starts its work on its own, so a run which
     * failed would leave no way to ask for it again: the call offers the work
     * on the page of the step instead.
     */
    void SetComparisonFailed();

    /**
     * @brief Tell the dialog that the difference of the capture is there.
     *
     * The call is the answer of the owner to the work of the step which
     * produced the difference: the step of the comparison of a snapshot capture
     * waits for it before the wizard walks on to the step of the analysis and
     * the fill.
     */
    void SetAnalysisReady();

    /**
     * @brief Show what the capture is doing right now.
     *
     * The report names the phase of the work, the entry which is being written
     * and how the entry ended. It is set while the workspaces are written, so
     * it is called far more often than a user can read.
     *
     * @param[in] progress The report of the work.
     */
    void SetFillProgress(const appbox::CaptureFillProgress& progress);

    /**
     * @brief Present the entries the capture could not write.
     *
     * The entries are listed in two pages: the ones which the workspaces cannot
     * express and the ones the capture could not write. The call is made while
     * the work runs as well, so the entries the analysis found are shown before
     * the workspaces are written.
     *
     * @param[in] report The report of the work.
     */
    void SetFillReport(const appbox::CaptureFillReport& report);

    /**
     * @brief Tell the dialog whether the workspaces are written right now.
     *
     * The workspaces are part of the session of the user, so the capture cannot
     * be dropped while they are written: the dialog greys the button which
     * drops it and refuses the close box of its title bar.
     *
     * @param[in] running Whether the workspaces are written right now.
     */
    void SetFillRunning(bool running);

    /**
     * @brief Tell the dialog that the whole work of the capture is over.
     *
     * The call enables the button which confirms the capture, which is what
     * lets the user end it.
     */
    void MarkFillComplete();

    /**
     * @brief Tell whether the user cancelled the capture.
     * @return true after a button or the close box dismissed the dialog before
     *         the user confirmed the review.
     */
    bool WasCancelled() const;

    /**
     * @brief Tell whether the user confirmed the review.
     * @return true after the owner applied the plan of the capture.
     */
    bool WasConfirmed() const;

private:
    /**
     * @brief The rows and the bar which follow the work of one step.
     */
    struct ProgressRows
    {
        /** Progress of the work of the step, which cycles while it runs. */
        wxGauge* gauge = nullptr;

        /** Value control of the action row of the step. */
        wxStaticText* action = nullptr;

        /** Value control of the object row of the step. */
        wxStaticText* object = nullptr;

        /** Value control of the entries row of the step. */
        wxStaticText* entries = nullptr;

        /** Value control of the elapsed row of the step. */
        wxStaticText* elapsed = nullptr;

        /**
         * @brief Value control of the state row of the step.
         *
         * The row belongs to the step of the analysis and the fill, which names
         * the state of the entry it writes: working, written or failed with the
         * reason. The other steps report no state, so their rows leave the
         * control empty.
         */
        wxStaticText* status = nullptr;
    };

    /**
     * @brief Build the tree of the steps and the page of every step.
     */
    void CreateLayout();

    /**
     * @brief Build the tree which names the steps of both capture modes.
     *
     * Both branches are built, so the user sees what the other mode would ask
     * for; only the branch of the mode which is picked carries the state of its
     * steps.
     */
    void CreateStageTree();

    /**
     * @brief Build the page of the choice of the mode.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateModePage(wxSimplebook& book);

    /**
     * @brief Build the prerequisites of the realtime capture and the button
     *        which restarts the packer as an administrator.
     *
     * The block is the extra of the realtime option of the group of the modes,
     * so it hangs below that option alone and stays visible while either mode
     * is picked.
     *
     * @param[in] parent Window the block hangs below, the box of the modes.
     * @return The panel which holds the block.
     */
    wxWindow* CreateModeRequirements(wxWindow* parent);

    /**
     * @brief Build the page which picks the source of the baseline of a
     *        snapshot capture.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateBaselineSourcePage(wxSimplebook& book);

    /**
     * @brief Build the page which picks the file the baseline of a snapshot
     *        capture is kept in.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateSnapshotFilePage(wxSimplebook& book);

    /**
     * @brief Build the page which reads the baseline of a snapshot capture.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateBaselinePage(wxSimplebook& book);

    /**
     * @brief Build the page of the recording of a realtime capture.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateRecordingPage(wxSimplebook& book);

    /**
     * @brief Build the page which waits for the installation of a snapshot
     *        capture.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateInstallationPage(wxSimplebook& book);

    /**
     * @brief Build the page of the comparison of a snapshot capture.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateComparisonPage(wxSimplebook& book);

    /**
     * @brief Build the page of the review.
     * @param[in] book Book the page belongs to.
     * @return The page.
     */
    wxPanel* CreateReviewPage(wxSimplebook& book);

    /**
     * @brief Add the bar and the rows which follow the work of a step.
     * @param[in] page Page which owns the controls.
     * @param[in] sizer Sizer the controls are added to.
     * @return The rows of the step.
     */
    static ProgressRows AddProgressRows(wxPanel* page, wxBoxSizer* sizer);

    /**
     * @brief Add the bar and the rows which follow the work of the step of the
     *        analysis and the fill.
     *
     * The rows of that step are the action, the object and the state of the
     * entry which is being written: the step reports one entry at a time and no
     * count of the entries it already wrote.
     *
     * @param[in] page Page which owns the controls.
     * @param[in] sizer Sizer the controls are added to.
     * @return The rows of the step.
     */
    static ProgressRows AddFillRows(wxPanel* page, wxBoxSizer* sizer);

    /**
     * @brief Add one label and value row to a grid of a page.
     *
     * The value control shortens a text which does not fit into its column in
     * the middle, so the start and the end of a path stay readable.
     *
     * @param[in] page Page which owns the controls.
     * @param[in,out] grid Grid the row is added to.
     * @param[in] label Label of the row, shown right aligned.
     * @return The value control of the row.
     */
    static wxStaticText* AddGridRow(wxPanel* page, wxFlexGridSizer& grid, const wxString& label);

    /**
     * @brief Find the rows which follow the work of a step.
     * @param[in] stage The step to look for.
     * @return The rows of the step, nullptr when the step reports no work.
     */
    const ProgressRows* RowsOf(appbox::CaptureWizardStage stage) const;

    /**
     * @brief Show the step the model holds.
     *
     * The tree, the description, the page and the buttons are refreshed from
     * one place, so a step is never half shown.
     */
    void RefreshStage();

    /**
     * @brief Show the state of the steps in the tree of both modes.
     */
    void RefreshTree();

    /**
     * @brief Show the prerequisites of the realtime capture below its option.
     *
     * The block belongs to the realtime option and stays on the page while
     * either mode is picked, so switching to the snapshot capture keeps what
     * the realtime capture asks for in sight. Only the button beside the list
     * follows the machine: it is greyed when the process already holds the
     * elevation or when the realtime capture cannot run at all.
     */
    void RefreshModePage();

    /**
     * @brief Show the path and the state of the baseline on the page of the
     *        file of the baseline.
     */
    void RefreshSnapshotFilePage();

    /**
     * @brief Leave the step the dialog shows.
     * @param[in] event Button event of the button which leaves the step.
     */
    void OnAdvance(wxCommandEvent& event);

    /**
     * @brief Dismiss the dialog and report the capture as cancelled.
     * @param[in] event Button event of the `Cancel` button.
     */
    void OnCancel(wxCommandEvent& event);

    /**
     * @brief Dismiss the dialog through the close box of the title bar.
     * @param[in] event Close event.
     */
    void OnClose(wxCloseEvent& event);

    /**
     * @brief Advance the bar and the clock of the step which shows work.
     * @param[in] event Timer event of the tick timer.
     */
    void OnTick(wxTimerEvent& event);

    /**
     * @brief Restart the packer as an administrator.
     * @param[in] event Button event of the `Restart as administrator` button.
     */
    void OnRestartAsAdministrator(wxCommandEvent& event);

    /**
     * @brief Pick the mode the user selected.
     * @param[in] event Radio button event of the group of the modes.
     */
    void OnModePicked(wxCommandEvent& event);

    /**
     * @brief Store the source of the baseline the user picked.
     *
     * The work of the source is not asked for here: the step which picks the
     * file asks for it, so switching the option never opens a dialog.
     *
     * @param[in] event Radio button event of the group of the sources.
     */
    void OnBaselinePicked(wxCommandEvent& event);

    /**
     * @brief Store the file of the baseline the user typed.
     * @param[in] event Text event of the control of the path.
     */
    void OnSnapshotPathChanged(wxCommandEvent& event);

    /**
     * @brief Ask the owner for the file of the baseline and store the answer.
     * @param[in] event Button event of the button which browses for the file.
     */
    void OnBrowseSnapshotPath(wxCommandEvent& event);

    /**
     * @brief Ask the owner to compare the machine with the baseline again.
     * @param[in] event Button event of the button which repeats the comparison.
     */
    void OnComparisonRequested(wxCommandEvent& event);

    /**
     * @brief Store the confirmation of the user that the installation is over.
     * @param[in] event Check box event of the confirmation of the installation.
     */
    void OnInstallationDoneToggled(wxCommandEvent& event);

    /**
     * @brief Show the time which passed since the dialog was created.
     */
    void RefreshElapsed();

    /**
     * @brief Dismiss the dialog without confirming the review.
     */
    void Dismiss();

    /**
     * @brief The steps of the wizard.
     */
    appbox::CaptureWizardModel model_;

    /**
     * @brief What the dialog tells its owner when it walks the steps.
     */
    StageHandler stage_handler_;

    /**
     * @brief What the dialog asks its owner when the baseline is picked.
     */
    BaselineHandler baseline_handler_;

    /**
     * @brief What the dialog asks its owner when the user browses for the file
     *        of the baseline.
     */
    BrowseHandler browse_handler_;

    /**
     * @brief Time the dialog was created, the elapsed time is measured from it.
     */
    std::chrono::steady_clock::time_point start_;

    /**
     * @brief Tree of the steps of both capture modes, which reports them and
     *        which the user cannot work.
     */
    ReportTreeCtrl* stage_tree_ = nullptr;

    /**
     * @brief The node of the step which picks the mode.
     */
    wxTreeItemId mode_node_;

    /**
     * @brief The node of the branch of every capture mode.
     */
    std::map<appbox::CaptureMode, wxTreeItemId> branch_nodes_;

    /**
     * @brief The node of every step of every capture mode.
     */
    std::map<appbox::CaptureMode, std::map<appbox::CaptureWizardStage, wxTreeItemId>> stage_nodes_;

    /**
     * @brief Book which holds the page of every step.
     *
     * The pages follow the order of `appbox::CaptureWizardStage`, so the index
     * of a page is the step itself. The steps a mode does not walk keep their
     * page, which is never shown.
     */
    wxSimplebook* book_ = nullptr;

    /**
     * @brief Description of the step which is shown, above the page of that
     *        step.
     */
    wxStaticText* description_ = nullptr;

    /**
     * @brief The description the control was filled with, before it was
     *        wrapped.
     *
     * The label of the control carries the line breaks the wrap inserted, so
     * the text it was built from is kept as well: only a description which
     * changed is set and wrapped again.
     */
    wxString description_text_;

    /**
     * @brief The capture modes the machine offers.
     */
    ModeChoiceGroup* mode_group_ = nullptr;

    /**
     * @brief Button which restarts the packer as an administrator.
     *
     * The button is greyed when the process already holds the elevation or when
     * the machine cannot run the realtime capture at all.
     */
    wxButton* restart_button_ = nullptr;

    /**
     * @brief The sources of the baseline of a snapshot capture.
     */
    ModeChoiceGroup* baseline_group_ = nullptr;

    /**
     * @brief The file the baseline of a snapshot capture is kept in.
     *
     * The control is editable, so the user can paste a path instead of walking
     * through the file dialog of the button beside it.
     */
    wxTextCtrl* snapshot_path_text_ = nullptr;

    /**
     * @brief Button which browses for the file of the baseline.
     */
    wxButton* browse_button_ = nullptr;

    /**
     * @brief State of the baseline, which follows the answer of the owner.
     */
    wxStaticText* baseline_text_ = nullptr;

    /**
     * @brief The text the state of the baseline was filled with, before it was
     *        wrapped.
     */
    wxString baseline_text_value_;

    /**
     * @brief The progress of the recording of a realtime capture.
     */
    ProgressRows recording_rows_;

    /**
     * @brief The progress of the baseline of a snapshot capture, which the page
     *        of the baseline shows.
     */
    ProgressRows baseline_rows_;

    /**
     * @brief The progress of the comparison of a snapshot capture.
     */
    ProgressRows comparison_rows_;

    /**
     * @brief Button which asks for the comparison again, shown when the run of
     *        the step failed.
     */
    wxButton* comparison_button_ = nullptr;

    /**
     * @brief Whether the comparison of the snapshot capture failed.
     */
    bool comparison_failed_ = false;

    /**
     * @brief Confirmation of the user that the installation is over, which the
     *        step of the recording of a realtime capture shows.
     */
    wxCheckBox* recording_done_box_ = nullptr;

    /**
     * @brief Confirmation of the user that the installation is over, which the
     *        step of the installation of a snapshot capture shows.
     */
    wxCheckBox* installation_done_box_ = nullptr;

    /**
     * @brief The bar and the rows of the step of the analysis and the fill.
     */
    ProgressRows fill_rows_;

    /**
     * @brief Description of the work of the capture on the last step.
     *
     * The line names the phase the capture is in: the analysis of the
     * difference, the repair of the entries or the write of the workspaces.
     */
    wxStaticText* analysis_text_ = nullptr;

    /**
     * @brief The text the description of the work of the analysis was filled
     *        with, before it was wrapped.
     *
     * The label of the control carries the line breaks the wrap inserted, so
     * the text it was built from is kept as well: only a text which changed is
     * set and wrapped again.
     */
    wxString analysis_text_value_;

    /**
     * @brief Book which switches between the two lists of the last step.
     */
    wxNotebook* fill_notebook_ = nullptr;

    /**
     * @brief The entries the capture could not write.
     */
    wxListBox* error_list_ = nullptr;

    /**
     * @brief The entries the capture could not express.
     */
    wxListBox* ignored_list_ = nullptr;

    /**
     * @brief Button which leaves the step the dialog shows.
     */
    wxButton* advance_button_ = nullptr;

    /**
     * @brief Button which drops the capture.
     *
     * The button is greyed while the workspaces are written and after they were
     * written, because a capture which reached that point cannot be taken back,
     * see `appbox::CaptureWizardModel::CanCancel()`.
     */
    wxButton* cancel_button_ = nullptr;

    /**
     * @brief Timer which advances the bar and the clock.
     */
    wxTimer* tick_timer_ = nullptr;

    /**
     * @brief Whether the user cancelled the capture.
     */
    bool cancelled_ = false;

    /**
     * @brief Whether the user confirmed the review.
     */
    bool confirmed_ = false;

    /**
     * @brief Disabler which keeps the user away from the other windows while
     *        the dialog is shown.
     */
    std::unique_ptr<wxWindowDisabler> disabler_;
};

#endif // APPBOX_PACKER_WIDGET_CAPTURE_WIZARD_HPP
