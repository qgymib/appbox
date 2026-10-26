#include "CaptureWizard.hpp"
#include "ModeChoiceGroup.hpp"
#include "core/BuildReport.hpp"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gauge.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstddef>
#include <string>

namespace
{

/** Border of the dialog and gap between its parts. */
constexpr int kBorder = 12;

/** Row gap of the grids of the pages. */
constexpr int kRowGap = 6;

/** Column gap of the grids of the pages and gap between the buttons. */
constexpr int kColumnGap = 12;

/** Width the tree of the steps reserves for itself. */
constexpr int kTreeWidth = 180;

/** Width the page of a step reserves for itself. */
constexpr int kBookWidth = 560;

/**
 * Height the page of a step reserves for itself.
 *
 * The height fits the tallest page, which is the one which picks the mode: it
 * explains both captures and lists the prerequisites of the realtime capture
 * below the realtime option.
 */
constexpr int kBookHeight = 430;

/** Height of the lists of the entries the capture could not write. */
constexpr int kNoticeListHeight = 100;

/** Time between two ticks of the clock of the dialog. */
constexpr int kTickInterval = 100;

/** Width the texts of a page are wrapped into. */
constexpr int kTextWidth = kBookWidth - 2 * kBorder;

/**
 * Height the description of a step reserves for itself, in lines of its font.
 *
 * The dialog is fitted once from the parts which never change and locks that
 * size, so the area above the page has to fit the description of every step:
 * the reserved height is the height of the longest description the steps carry.
 */
constexpr int kDescriptionLines = 3;

/**
 * Height the status of the analysis of the review reserves for itself, in lines
 * of its font.
 *
 * The status of the analysis is the longest text of the page and it is set
 * while the review is shown, so the height of two lines is reserved for it: the
 * rows below it do not move when the text grows, and a text which needs a
 * second line is not cut off at the bottom of its row.
 */
constexpr int kAnalysisLines = 2;

/**
 * @brief Replace the label of a static text only when it changed.
 *
 * A label which is set again is a repaint without a reason, and the rows of a
 * step are rewritten on every report of its work.
 *
 * @param[in,out] text Control whose label is replaced.
 * @param[in] label New label.
 */
void SetLabelIfChanged(wxStaticText* text, const wxString& label)
{
    if (text->GetLabel() != label)
    {
        text->SetLabel(label);
    }
}

/**
 * @brief The mark which names a step the wizard passed.
 * @return The check mark.
 */
wxString DoneMark()
{
    return wxString::FromUTF8("\xE2\x9C\x93");
}

/**
 * @brief The mark which names a prerequisite the machine does not meet.
 * @return The cross mark.
 */
wxString MissingMark()
{
    return wxString::FromUTF8("\xE2\x9C\x97");
}

/**
 * @brief The name of a step of the capture, the way the tree shows it.
 * @param[in] stage The step.
 * @return The name of the step.
 */
wxString StageLabel(appbox::CaptureWizardStage stage)
{
    switch (stage)
    {
    case appbox::CaptureWizardStage::Mode:
        return "Capture Mode";
    case appbox::CaptureWizardStage::Snapshot:
        return "Snapshot";
    case appbox::CaptureWizardStage::SnapshotFile:
        return "Snapshot File";
    case appbox::CaptureWizardStage::Baseline:
        return "Baseline";
    case appbox::CaptureWizardStage::Recording:
        return "Recording";
    case appbox::CaptureWizardStage::Installation:
        return "Installation";
    case appbox::CaptureWizardStage::Comparison:
        return "Comparison";
    case appbox::CaptureWizardStage::Review:
        return "Analysis and Fill";
    case appbox::CaptureWizardStage::Done:
        return "Done";
    }

    return {};
}

/**
 * @brief What the dialog says about a step, above the page of that step.
 * @param[in] model The rules of the wizard, which name the step and the source
 *            of the baseline.
 * @return The description of the step.
 */
wxString StageDescription(const appbox::CaptureWizardModel& model)
{
    const auto mode = model.Mode();

    switch (model.Stage())
    {
    case appbox::CaptureWizardStage::Mode:
        return "Pick the way the capture reads the changes of an installation. Both modes end in the same review, "
               "which is where the changes are written into the workspaces.";
    case appbox::CaptureWizardStage::Snapshot:
        return "Pick where the baseline of the comparison comes from: the state of the machine, which AppBox reads "
               "when the file of the baseline is set, or the state a snapshot file an earlier run wrote already "
               "holds.";
    case appbox::CaptureWizardStage::SnapshotFile:
        return model.BaselineSource() == appbox::SnapshotBaselineSource::New
                   ? "Set the file the baseline is kept in. AppBox reads the state of the machine into it on the "
                     "step which follows, and the comparison opens the same file again after the installation."
                   : "Set the snapshot file an earlier run wrote. AppBox uses the state it holds as the baseline on "
                     "the step which follows, and the state after the installation is written into the same file.";
    case appbox::CaptureWizardStage::Baseline:
        return model.BaselineSource() == appbox::SnapshotBaselineSource::New
                   ? "AppBox reads the state of the machine into the file you named and counts the entries of the "
                     "run. A run which fails shows the step which names the file again."
                   : "AppBox uses the state the snapshot file holds as the baseline and counts the entries of the "
                     "file. A run which fails shows the step which names the file again.";
    case appbox::CaptureWizardStage::Recording:
        return "Install the program outside AppBox: the recording follows the changes of the machine in the "
               "meantime. Tick the box below when the installation is complete, then press 'Next'.";
    case appbox::CaptureWizardStage::Installation:
        return "Install the program outside AppBox: the state of the machine changes in the meantime and the "
               "comparison reads it afterwards. Tick the box below when the installation is complete, then press "
               "'Next'.";
    case appbox::CaptureWizardStage::Comparison:
        return "The state of the machine is read again and compared with the baseline; the difference of the two "
               "reads is what the step of the analysis and the fill writes.";
    case appbox::CaptureWizardStage::Review:
        return mode == appbox::CaptureMode::Snapshot
                   ? "The difference of the two reads is analyzed and written into the workspaces; the entries which "
                     "cannot be written are listed below. 'Cancel' is greyed while the workspaces are written."
                   : "The changes of the recording are analyzed and written into the workspaces; the entries which "
                     "cannot be written are listed below. 'Cancel' is greyed while the workspaces are written.";
    case appbox::CaptureWizardStage::Done:
        return "The changes of the capture were written into the workspaces.";
    }

    return {};
}

/**
 * @brief What the dialog says about the file of the baseline.
 *
 * The step only names the file: the state of the baseline belongs to the step
 * which follows, so the text says whether the file is named yet and what the
 * next step does with it.
 *
 * @param[in] model The rules of the wizard.
 * @return The state of the file.
 */
wxString BaselineStatusText(const appbox::CaptureWizardModel& model)
{
    const bool created = model.BaselineSource() == appbox::SnapshotBaselineSource::New;

    if (model.SnapshotPath().empty())
    {
        return created ? "Set the file the baseline is kept in, then press 'Next'."
                       : "Set the snapshot file an earlier run wrote, then press 'Next'.";
    }

    return created ? "Press 'Next' to read the state of the machine into the file."
                   : "Press 'Next' to use the state the file holds as the baseline.";
}

/**
 * @brief Build the text of the state row of the step of the analysis and the
 *        fill.
 *
 * @param[in] state The state of the entry which is being written.
 * @param[in] reason The reason of a failure, empty for every other state.
 * @return The text of the row.
 */
wxString FillStateText(appbox::CaptureFillState state, const wxString& reason)
{
    switch (state)
    {
    case appbox::CaptureFillState::Working:
        return "Working";
    case appbox::CaptureFillState::Succeeded:
        return "Succeeded";
    case appbox::CaptureFillState::Failed:
        return reason.empty() ? wxString("Failed") : wxString::Format("Failed: %s", reason);
    }

    return {};
}

/**
 * @brief Build the line which counts the entries a list does not show.
 * @param[in] total Number of the entries the capture reported.
 * @param[in] shown Number of the entries the list holds.
 * @return The line, empty when every entry is listed.
 */
wxString FurtherNoticesText(std::size_t total, std::size_t shown)
{
    if (total <= shown)
    {
        return {};
    }

    return wxString::Format("and %llu further entries are not listed", static_cast<unsigned long long>(total - shown));
}

/**
 * @brief Replace the label of a static text and wrap it to the width of the
 *        column.
 *
 * The text the control was filled with before is kept beside it, because the
 * label of a wrapped control carries the line breaks the wrap inserted: only a
 * text which changed is set and wrapped again, which keeps a refresh from
 * repainting a control which says the same thing.
 *
 * @param[in,out] text Control whose label is replaced.
 * @param[in] label The text to show, before it is wrapped.
 * @param[in,out] shown The text the control was filled with before.
 */
void SetWrappedLabel(wxStaticText* text, const wxString& label, wxString& shown)
{
    if (label == shown)
    {
        return;
    }

    shown = label;
    text->SetLabel(label);
    text->Wrap(kTextWidth);
}

/**
 * @brief Build the options of the capture modes.
 *
 * The options follow the order of `appbox::CaptureModeNames()`, so the index of
 * an option is the mode itself, and every option carries the text of
 * `appbox::CaptureModeDescription()`. A mode the machine cannot run is offered
 * as an option which cannot be picked, because the model refuses to pick it.
 *
 * @param[in] model The rules of the wizard, which tell which modes are offered.
 * @return The capture modes with the explanation of each one.
 */
std::vector<ModeChoice> CaptureModeChoices(const appbox::CaptureWizardModel& model)
{
    std::vector<ModeChoice> choices;
    for (const auto& name : appbox::CaptureModeNames())
    {
        const auto mode = static_cast<appbox::CaptureMode>(choices.size());
        choices.push_back(
            { wxString(name), wxString(appbox::CaptureModeDescription(mode)), model.CanSelectMode(mode) });
    }
    return choices;
}

/**
 * @brief Build the options of the sources of the baseline of a snapshot capture.
 *
 * The options follow the order of `appbox::SnapshotBaselineSource`, so the
 * index of an option is the source itself.
 *
 * @return The two sources with the explanation of each one.
 */
std::vector<ModeChoice> BaselineSourceChoices()
{
    return {
        { "Create a new snapshot",
         "AppBox reads the state of the machine now and keeps it in a new snapshot file. The file is the "
          "baseline the comparison reads after the installation." },
        { "Use an existing snapshot",
         "AppBox uses the state a snapshot file an earlier run wrote already holds as the baseline. The state "
          "after the installation is written into the same file." }
    };
}

} // namespace

CaptureWizard::CaptureWizard(wxWindow* parent, const wxString& title, const appbox::CaptureRequirements& requirements)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), model_(requirements),
      start_(std::chrono::steady_clock::now())
{
    CreateLayout();

    Bind(wxEVT_CLOSE_WINDOW, &CaptureWizard::OnClose, this);

    /*
     * The dialog is fitted once from the parts which never change and locks
     * that size: the pages of the steps reserve their own size, so walking
     * through them cannot move the buttons of the dialog.
     */
    Fit();
    const wxSize size = GetSize();
    SetMinSize(size);
    SetMaxSize(size);

    RefreshStage();

    tick_timer_ = new wxTimer(this);
    Bind(wxEVT_TIMER, &CaptureWizard::OnTick, this, tick_timer_->GetId());
    tick_timer_->Start(kTickInterval);
}

CaptureWizard::~CaptureWizard()
{
    if (tick_timer_ != nullptr)
    {
        tick_timer_->Stop();
        delete tick_timer_;
        tick_timer_ = nullptr;
    }

    /* A dialog which was never hidden has to give the windows back. */
    disabler_.reset();
}

void CaptureWizard::CreateLayout()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* body = new wxBoxSizer(wxHORIZONTAL);

    /*
     * The tree reports the steps of the capture and is not a control: it draws
     * no button which expands or collapses a branch and it takes no input at
     * all, see ReportTreeCtrl.
     */
    stage_tree_ = new ReportTreeCtrl(this, wxSize(kTreeWidth, -1));
    CreateStageTree();
    body->Add(stage_tree_, 0, wxEXPAND | wxRIGHT, kBorder);

    /*
     * The information about the step is the column of its page: the dialog says
     * what the step is about above the page which does the work of the step, so
     * the tree on the left stays the plain list of the steps of the capture.
     */
    auto* information = new wxBoxSizer(wxVERTICAL);

    description_ =
        new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    description_->SetMinSize(wxSize(kTextWidth, kDescriptionLines * description_->GetCharHeight()));
    information->Add(description_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    book_ = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, wxSize(kBookWidth, kBookHeight));
    book_->SetMinSize(wxSize(kBookWidth, kBookHeight));

    /*
     * The pages follow the order of the steps, so the index of a page is the
     * step itself and a step which is shown can select its page without a map.
     */
    book_->AddPage(CreateModePage(*book_), "Capture Mode");
    book_->AddPage(CreateBaselineSourcePage(*book_), "Snapshot");
    book_->AddPage(CreateSnapshotFilePage(*book_), "Snapshot File");
    book_->AddPage(CreateBaselinePage(*book_), "Baseline");
    book_->AddPage(CreateRecordingPage(*book_), "Recording");
    book_->AddPage(CreateInstallationPage(*book_), "Installation");
    book_->AddPage(CreateComparisonPage(*book_), "Comparison");
    book_->AddPage(CreateReviewPage(*book_), "Review");

    information->Add(book_, 1, wxEXPAND);
    body->Add(information, 1, wxEXPAND);
    sizer->Add(body, 1, wxEXPAND | wxALL, kBorder);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();

    advance_button_ = new wxButton(this, wxID_ANY, "Next");
    advance_button_->SetDefault();
    cancel_button_ = new wxButton(this, wxID_CANCEL, "Cancel");

    buttons->Add(advance_button_, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, kColumnGap);
    buttons->Add(cancel_button_, 0, wxALIGN_CENTRE_VERTICAL);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    Bind(wxEVT_BUTTON, &CaptureWizard::OnAdvance, this, advance_button_->GetId());
    Bind(wxEVT_BUTTON, &CaptureWizard::OnCancel, this, wxID_CANCEL);

    SetSizer(sizer);
}

void CaptureWizard::CreateStageTree()
{
    const auto root = stage_tree_->AddRoot("Capture");
    mode_node_ = stage_tree_->AppendItem(root, StageLabel(appbox::CaptureWizardStage::Mode));

    /*
     * Both branches are built, so the user sees what the mode which is not
     * picked would ask for. The step which picks the mode is shared and hangs
     * below the root, so it is not part of a branch.
     */
    for (const auto mode : { appbox::CaptureMode::Realtime, appbox::CaptureMode::Snapshot })
    {
        const auto branch = stage_tree_->AppendItem(root, wxString(appbox::CaptureModeName(mode)));
        branch_nodes_[mode] = branch;

        for (const auto stage : appbox::CaptureStages(mode))
        {
            if (stage == appbox::CaptureWizardStage::Mode || stage == appbox::CaptureWizardStage::Done)
            {
                continue;
            }

            stage_nodes_[mode][stage] = stage_tree_->AppendItem(branch, StageLabel(stage));
        }
    }

    stage_tree_->ExpandAll();
}

wxPanel* CaptureWizard::CreateModePage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /*
     * The options are ordered like the enumeration of the modes, so the
     * selection of the group is the mode itself, and every option carries the
     * explanation of its mode below its name, see ModeChoiceGroup. The
     * prerequisites and the restart hang below the realtime option alone, so
     * they are the extra of that option and not a block of the page.
     */
    mode_group_ = new ModeChoiceGroup(
        page, "Capture mode", CaptureModeChoices(model_), [this](wxWindow* parent, int index) -> wxWindow* {
            return index == static_cast<int>(appbox::CaptureMode::Realtime) ? CreateModeRequirements(parent) : nullptr;
        });
    mode_group_->SetSelection(static_cast<int>(model_.Mode()));
    mode_group_->Bind(wxEVT_RADIOBUTTON, &CaptureWizard::OnModePicked, this);
    sizer->Add(mode_group_, 0, wxEXPAND | wxALL, kBorder);

    page->SetSizer(sizer);
    return page;
}

wxWindow* CaptureWizard::CreateModeRequirements(wxWindow* parent)
{
    /*
     * The prerequisites of the realtime capture are listed with the state of
     * the machine, so the step explains itself instead of reporting a failure
     * after the user pressed the button which leaves it. The detail of a
     * prerequisite is not written down: the mark which names the state and the
     * name of the prerequisite are what the user reads.
     */
    auto* panel = new wxPanel(parent);
    auto* content = new wxBoxSizer(wxVERTICAL);

    auto* grid = new wxFlexGridSizer(2, kRowGap, kColumnGap);
    grid->AddGrowableCol(1, 1);
    for (const auto& requirement : appbox::DescribeRequirements(model_.Requirements()))
    {
        auto* mark = new wxStaticText(panel, wxID_ANY, requirement.met ? DoneMark() : MissingMark());
        auto* name = new wxStaticText(panel, wxID_ANY, wxString(requirement.name));

        grid->Add(mark, 0, wxALIGN_RIGHT | wxALIGN_TOP);
        grid->Add(name, 1, wxEXPAND | wxALIGN_TOP);
    }

    content->Add(grid, 0, wxEXPAND);

    restart_button_ = new wxButton(panel, wxID_ANY, "Restart as administrator");
    content->Add(restart_button_, 0, wxTOP, kRowGap);
    Bind(wxEVT_BUTTON, &CaptureWizard::OnRestartAsAdministrator, this, restart_button_->GetId());

    panel->SetSizer(content);

    return panel;
}

wxPanel* CaptureWizard::CreateBaselineSourcePage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /*
     * The options are ordered like the enumeration of the sources, so the
     * selection of the group is the source itself. Picking a source only stores
     * it: the work of the source is asked for on the step which picks the file,
     * so a click on an option never opens a dialog of its own.
     */
    baseline_group_ = new ModeChoiceGroup(page, "Baseline", BaselineSourceChoices());
    baseline_group_->SetSelection(static_cast<int>(model_.BaselineSource()));
    baseline_group_->Bind(wxEVT_RADIOBUTTON, &CaptureWizard::OnBaselinePicked, this);
    sizer->Add(baseline_group_, 0, wxEXPAND | wxALL, kBorder);

    page->SetSizer(sizer);
    return page;
}

wxPanel* CaptureWizard::CreateSnapshotFilePage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /*
     * The path is editable, so the user can paste one instead of walking
     * through the file dialog: the button beside the control only fills it in.
     */
    auto* path_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* path_label = new wxStaticText(page, wxID_ANY, "Snapshot file:");
    path_sizer->Add(path_label, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, kColumnGap);

    snapshot_path_text_ = new wxTextCtrl(page, wxID_ANY);
    path_sizer->Add(snapshot_path_text_, 1, wxALIGN_CENTRE_VERTICAL | wxRIGHT, kColumnGap);
    Bind(wxEVT_TEXT, &CaptureWizard::OnSnapshotPathChanged, this, snapshot_path_text_->GetId());

    browse_button_ = new wxButton(page, wxID_ANY, "Browse...");
    path_sizer->Add(browse_button_, 0, wxALIGN_CENTRE_VERTICAL);
    Bind(wxEVT_BUTTON, &CaptureWizard::OnBrowseSnapshotPath, this, browse_button_->GetId());

    sizer->Add(path_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);

    baseline_text_ = new wxStaticText(page, wxID_ANY, wxEmptyString);
    sizer->Add(baseline_text_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);

    page->SetSizer(sizer);
    return page;
}

wxPanel* CaptureWizard::CreateBaselinePage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /*
     * The step starts its work as soon as the wizard enters it, so the page is
     * the bar and the rows which follow that work: what the step does is said
     * by the description above the page.
     */
    baseline_rows_ = AddProgressRows(page, sizer);

    page->SetSizer(sizer);
    return page;
}

wxPanel* CaptureWizard::CreateRecordingPage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    recording_rows_ = AddProgressRows(page, sizer);

    /*
     * The step is left on the decision of the user, and the decision is the
     * confirmation that the installation is over: the box is what opens the way
     * to the next step, so the button which leaves the step waits for it.
     */
    recording_done_box_ =
        new wxCheckBox(page, wxID_ANY, "The installation is complete and the recorded changes are final.");
    sizer->Add(recording_done_box_, 0, wxLEFT | wxRIGHT | wxBOTTOM, kBorder);
    Bind(wxEVT_CHECKBOX, &CaptureWizard::OnInstallationDoneToggled, this, recording_done_box_->GetId());

    page->SetSizer(sizer);
    return page;
}

wxPanel* CaptureWizard::CreateInstallationPage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* note = new wxStaticText(page, wxID_ANY,
                                  "AppBox reads the state of the machine again when you leave this step, so the "
                                  "installation has to be over before you press 'Next'.");
    note->Wrap(kTextWidth);
    sizer->Add(note, 0, wxEXPAND | wxALL, kBorder);

    /*
     * The confirmation is the one of the recording step of the realtime
     * capture: it is what opens the way to the next step.
     */
    installation_done_box_ =
        new wxCheckBox(page, wxID_ANY, "The installation is complete and the machine is ready for the second read.");
    sizer->Add(installation_done_box_, 0, wxLEFT | wxRIGHT | wxBOTTOM, kBorder);
    Bind(wxEVT_CHECKBOX, &CaptureWizard::OnInstallationDoneToggled, this, installation_done_box_->GetId());

    page->SetSizer(sizer);
    return page;
}

wxPanel* CaptureWizard::CreateComparisonPage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    comparison_rows_ = AddProgressRows(page, sizer);

    /*
     * The step starts its work as soon as it is entered, so the button is only
     * there for a run which failed: it asks for the comparison again instead of
     * leaving the step without a way out.
     */
    comparison_button_ = new wxButton(page, wxID_ANY, "Compare the machine with the baseline again");
    comparison_button_->Hide();
    sizer->Add(comparison_button_, 0, wxLEFT | wxRIGHT | wxBOTTOM, kBorder);
    Bind(wxEVT_BUTTON, &CaptureWizard::OnComparisonRequested, this, comparison_button_->GetId());

    page->SetSizer(sizer);
    return page;
}

wxPanel* CaptureWizard::CreateReviewPage(wxSimplebook& book)
{
    auto* page = new wxPanel(&book);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /*
     * The line names the phase the capture is in: the analysis of the
     * difference, the repair of the entries or the write of the workspaces.
     */
    analysis_text_ = new wxStaticText(page, wxID_ANY, "Analyzing the changes of the capture", wxDefaultPosition,
                                      wxDefaultSize, wxST_NO_AUTORESIZE);
    analysis_text_->SetMinSize(wxSize(-1, kAnalysisLines * analysis_text_->GetCharHeight()));
    sizer->Add(analysis_text_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kBorder);

    fill_rows_ = AddFillRows(page, sizer);

    /*
     * The entries the capture could not write are listed in two pages: the ones
     * the workspaces cannot express and the ones the capture could not write.
     */
    fill_notebook_ = new wxNotebook(page, wxID_ANY);

    error_list_ = new wxListBox(fill_notebook_, wxID_ANY, wxDefaultPosition, wxSize(-1, kNoticeListHeight));
    error_list_->SetMinSize(wxSize(-1, kNoticeListHeight));
    fill_notebook_->AddPage(error_list_, "Errors", true);

    ignored_list_ = new wxListBox(fill_notebook_, wxID_ANY, wxDefaultPosition, wxSize(-1, kNoticeListHeight));
    ignored_list_->SetMinSize(wxSize(-1, kNoticeListHeight));
    fill_notebook_->AddPage(ignored_list_, "Ignored");

    sizer->Add(fill_notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    page->SetSizer(sizer);
    return page;
}

wxStaticText* CaptureWizard::AddGridRow(wxPanel* page, wxFlexGridSizer& grid, const wxString& label)
{
    auto* label_text = new wxStaticText(page, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);

    /*
     * The middle ellipsis only works together with wxST_NO_AUTORESIZE: without
     * it every SetLabel() grows the control to the full width of the text, which
     * pushes it past the size the dialog locked.
     */
    auto* value_text = new wxStaticText(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                        wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);

    grid.Add(label_text, 0, wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL);
    grid.Add(value_text, 1, wxEXPAND | wxALIGN_CENTRE_VERTICAL);
    return value_text;
}

CaptureWizard::ProgressRows CaptureWizard::AddProgressRows(wxPanel* page, wxBoxSizer* sizer)
{
    ProgressRows rows;

    rows.gauge = new wxGauge(page, wxID_ANY, 100);
    sizer->Add(rows.gauge, 0, wxEXPAND | wxALL, kBorder);

    auto* grid = new wxFlexGridSizer(2, kRowGap, kColumnGap);
    grid->AddGrowableCol(1, 1);

    rows.action = AddGridRow(page, *grid, "Action:");
    rows.object = AddGridRow(page, *grid, "Object:");
    rows.entries = AddGridRow(page, *grid, "Entries:");
    rows.elapsed = AddGridRow(page, *grid, "Elapsed:");

    sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);
    return rows;
}

CaptureWizard::ProgressRows CaptureWizard::AddFillRows(wxPanel* page, wxBoxSizer* sizer)
{
    ProgressRows rows;

    rows.gauge = new wxGauge(page, wxID_ANY, 100);
    sizer->Add(rows.gauge, 0, wxEXPAND | wxALL, kBorder);

    sizer->Add(new wxStaticLine(page), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    auto* grid = new wxFlexGridSizer(2, kRowGap, kColumnGap);
    grid->AddGrowableCol(1, 1);

    rows.action = AddGridRow(page, *grid, "Action:");
    rows.object = AddGridRow(page, *grid, "Object:");
    rows.status = AddGridRow(page, *grid, "Status:");

    sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    sizer->Add(new wxStaticLine(page), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);
    return rows;
}

bool CaptureWizard::Show(bool show)
{
    if (show && !IsShown())
    {
        disabler_ = std::make_unique<wxWindowDisabler>(this);
    }

    if (!show)
    {
        /*
         * The other windows are enabled before the dialog is hidden: a window
         * which is still disabled cannot take the activation back when the
         * active dialog disappears, and Windows activates another application
         * instead. The modal dialog of wxWidgets leaves its modal loop for the
         * same reason before it hides itself.
         */
        disabler_.reset();
    }

    const bool changed = wxDialog::Show(show);

    if (!show)
    {
        if (changed)
        {
            /*
             * The dialog is owned by the window which opened it, but Windows
             * does not have to activate the owner when an owned window is
             * hidden. Raising the owner puts the focus back on the window the
             * dialog was opened from.
             */
            if (auto* owner = wxDynamicCast(GetParent(), wxTopLevelWindow); owner != nullptr && owner->IsShown())
            {
                owner->Raise();
            }
        }

        if (tick_timer_ != nullptr && tick_timer_->IsRunning())
        {
            tick_timer_->Stop();
        }
    }

    return changed;
}

void CaptureWizard::SetStageHandler(StageHandler handler)
{
    stage_handler_ = std::move(handler);
}

void CaptureWizard::SetBaselineHandler(BaselineHandler handler)
{
    baseline_handler_ = std::move(handler);
}

void CaptureWizard::SetBrowseHandler(BrowseHandler handler)
{
    browse_handler_ = std::move(handler);
}

void CaptureWizard::SetProgressStatus(appbox::CaptureWizardStage stage, const wxString& action, const wxString& object,
                                      const wxString& entries)
{
    if (model_.Stage() != stage)
    {
        /* The step is over, so a late report must not touch the page. */
        return;
    }

    const auto* rows = RowsOf(stage);
    if (rows == nullptr)
    {
        return;
    }

    SetLabelIfChanged(rows->action, action);
    SetLabelIfChanged(rows->object, object);
    if (rows->entries != nullptr)
    {
        SetLabelIfChanged(rows->entries, entries);
    }
}

void CaptureWizard::SetAnalysisStatus(const wxString& text)
{
    if (model_.Stage() != appbox::CaptureWizardStage::Review)
    {
        return;
    }

    SetWrappedLabel(analysis_text_, text, analysis_text_value_);
    analysis_text_->GetParent()->Layout();
}

void CaptureWizard::SetBaselineReady(bool ready)
{
    /*
     * The answer ends the request, whatever it says: a run which produced no
     * baseline shows the step which names the file again, so the path can be
     * fixed and the work asked for again instead of leaving the step without a
     * way out.
     */
    model_.SetBaselineReady(ready);

    RefreshStage();
}

void CaptureWizard::SetComparisonFailed()
{
    comparison_failed_ = true;
    RefreshStage();
}

void CaptureWizard::SetAnalysisReady()
{
    /* The comparison which produced the difference is over, so a failed run is not offered anymore. */
    comparison_failed_ = false;
    model_.MarkAnalysisReady();

    RefreshStage();
}

void CaptureWizard::SetFillProgress(const appbox::CaptureFillProgress& progress)
{
    if (model_.Stage() != appbox::CaptureWizardStage::Review)
    {
        /* The step is over, so a late report must not touch the page. */
        return;
    }

    SetLabelIfChanged(fill_rows_.action, wxString(progress.action));
    SetLabelIfChanged(fill_rows_.object, wxString(progress.object));
    SetLabelIfChanged(fill_rows_.status, FillStateText(progress.state, wxString(progress.message)));
}

void CaptureWizard::SetFillReport(const appbox::CaptureFillReport& report)
{
    const auto show = [](wxListBox* list, const std::vector<appbox::CaptureFillNotice>& entries, std::size_t total) {
        list->Clear();
        for (const auto& entry : entries)
        {
            list->Append(wxString(entry.message));
        }

        /*
         * The report lists the first entries of a capture which left out
         * thousands of them, so the ones the list does not hold are counted.
         */
        const auto further = FurtherNoticesText(total, entries.size());
        if (!further.empty())
        {
            list->Append(further);
        }
    };

    show(error_list_, report.errors, report.total_errors);
    show(ignored_list_, report.ignored, report.total_ignored);
}

void CaptureWizard::SetFillRunning(bool running)
{
    model_.SetFillRunning(running);
    RefreshStage();
}

void CaptureWizard::MarkFillComplete()
{
    model_.MarkFillComplete();
    RefreshStage();
}

bool CaptureWizard::WasCancelled() const
{
    return cancelled_;
}

bool CaptureWizard::WasConfirmed() const
{
    return confirmed_;
}

const CaptureWizard::ProgressRows* CaptureWizard::RowsOf(appbox::CaptureWizardStage stage) const
{
    switch (stage)
    {
    case appbox::CaptureWizardStage::Baseline:
        return &baseline_rows_;
    case appbox::CaptureWizardStage::Recording:
        return &recording_rows_;
    case appbox::CaptureWizardStage::Comparison:
        return &comparison_rows_;
    case appbox::CaptureWizardStage::Review:
        return &fill_rows_;
    case appbox::CaptureWizardStage::Mode:
    case appbox::CaptureWizardStage::Snapshot:
    case appbox::CaptureWizardStage::SnapshotFile:
    case appbox::CaptureWizardStage::Installation:
    case appbox::CaptureWizardStage::Done:
        return nullptr;
    }

    return nullptr;
}

void CaptureWizard::RefreshStage()
{
    const auto stage = model_.Stage();

    RefreshTree();
    RefreshModePage();
    RefreshSnapshotFilePage();

    if (stage != appbox::CaptureWizardStage::Done)
    {
        book_->SetSelection(static_cast<int>(stage));
    }

    SetWrappedLabel(description_, StageDescription(model_), description_text_);

    /*
     * Both steps which wait for the installation follow the model, so the page
     * of a mode which is picked later cannot show a confirmation the other one
     * collected.
     */
    recording_done_box_->SetValue(model_.IsInstallationDone());
    installation_done_box_->SetValue(model_.IsInstallationDone());

    advance_button_->SetLabel(wxString(model_.AdvanceLabel()));
    advance_button_->Enable(model_.CanAdvance());

    /*
     * The capture cannot be dropped while the workspaces are written and after
     * they were written, so the button which drops it follows the model.
     */
    cancel_button_->Enable(model_.CanCancel());

    /*
     * The offer of a comparison which failed follows the run of the step. The
     * button is created hidden, which means the sizer never placed it, so the
     * page is laid out again when it appears: without the pass it would be
     * drawn at the corner of the page, over the bar and the rows of the step.
     */
    if (comparison_button_->Show(comparison_failed_))
    {
        comparison_button_->GetParent()->Layout();
    }

    RefreshElapsed();
}

void CaptureWizard::RefreshTree()
{
    const auto colour_of = [](bool grey) {
        return wxSystemSettings::GetColour(grey ? wxSYS_COLOUR_GRAYTEXT : wxSYS_COLOUR_WINDOWTEXT);
    };

    /*
     * A step which the picked mode does not walk carries no state of its own,
     * and the branch of the other mode is greyed out as a whole. The step the
     * dialog shows is the one the bold font marks, because the tree is a report
     * which holds no selection.
     */
    const auto apply = [this, &colour_of](const wxTreeItemId& node, const wxString& label,
                                          appbox::CaptureStageState state, bool active) {
        wxFont font = stage_tree_->GetItemFont(node);
        font.SetWeight(active && state == appbox::CaptureStageState::Current ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        stage_tree_->SetItemFont(node, font);

        stage_tree_->SetItemTextColour(node, colour_of(!active || state == appbox::CaptureStageState::Pending));
        stage_tree_->SetItemText(
            node, (state == appbox::CaptureStageState::Done ? DoneMark() + wxString(" ") : wxString("")) + label);
    };

    apply(mode_node_, StageLabel(appbox::CaptureWizardStage::Mode), model_.StateOf(appbox::CaptureWizardStage::Mode),
          true);

    for (const auto mode : { appbox::CaptureMode::Realtime, appbox::CaptureMode::Snapshot })
    {
        const bool picked = mode == model_.Mode();
        const auto branch = branch_nodes_[mode];

        stage_tree_->SetItemTextColour(branch, colour_of(!picked));

        const auto& nodes = stage_nodes_[mode];
        for (const auto stage : appbox::CaptureStages(mode))
        {
            const auto found = nodes.find(stage);
            if (found == nodes.end())
            {
                continue;
            }

            apply(found->second, StageLabel(stage), picked ? model_.StateOf(stage) : appbox::CaptureStageState::Pending,
                  picked);
        }
    }
}

void CaptureWizard::RefreshModePage()
{
    const auto& requirements = model_.Requirements();

    /*
     * The button is offered with the realtime option and is greyed when the
     * process already holds the elevation or when the machine cannot run the
     * realtime capture at all. The prerequisites beside it stay visible while
     * either mode is picked, so picking the snapshot capture does not hide what
     * the realtime capture asks for.
     */
    restart_button_->Enable(requirements.supported && !requirements.elevated);
}

void CaptureWizard::RefreshSnapshotFilePage()
{
    /*
     * The control is what the user types into, so it is only written when the
     * path was set from elsewhere: writing it back would move the cursor of a
     * user who is in the middle of a path. A path which is set anew drops the
     * baseline which belongs to the file it named before.
     */
    const wxString path(model_.SnapshotPath());
    if (snapshot_path_text_->GetValue() != path)
    {
        snapshot_path_text_->ChangeValue(path);
    }

    SetWrappedLabel(baseline_text_, BaselineStatusText(model_), baseline_text_value_);
}

void CaptureWizard::OnAdvance(wxCommandEvent&)
{
    const auto from = model_.Stage();

    if (from == appbox::CaptureWizardStage::Review)
    {
        /*
         * The whole work of the capture is over when the button is offered, so
         * the confirmation only ends the capture: the workspaces were written
         * by the owner before the button was enabled.
         */
        confirmed_ = true;
        model_.Advance();
        Show(false);
        return;
    }

    if (!model_.Advance())
    {
        return;
    }

    const auto to = model_.Stage();

    if (to == appbox::CaptureWizardStage::Comparison)
    {
        /* The step starts its work on its own, so the offer of a failed run is over. */
        comparison_failed_ = false;
    }

    RefreshStage();

    if (stage_handler_)
    {
        stage_handler_(from, to);
    }

    /*
     * The step of the baseline asks the owner for the work of the source as
     * soon as it is shown, so the bar and the rows of its page are there when
     * the first report of the run arrives.
     */
    if (to == appbox::CaptureWizardStage::Baseline && baseline_handler_ != nullptr)
    {
        baseline_handler_(model_.BaselineSource(), model_.SnapshotPath());
    }
}

void CaptureWizard::OnCancel(wxCommandEvent&)
{
    if (!model_.CanCancel())
    {
        /* The workspaces are written or were written, so the capture is over. */
        return;
    }

    cancelled_ = true;
    Show(false);
}

void CaptureWizard::OnClose(wxCloseEvent& event)
{
    /*
     * The close box of the title bar asks the owner to drop the capture, which
     * is refused while the workspaces are written: the button which drops the
     * capture is greyed for the same reason.
     */
    if (!model_.CanCancel())
    {
        event.Veto();
        return;
    }

    cancelled_ = true;
    event.Skip(false);
    Show(false);
}

void CaptureWizard::OnTick(wxTimerEvent&)
{
    const auto* rows = RowsOf(model_.Stage());
    if (rows == nullptr)
    {
        return;
    }

    /* The total of the work of a step is not known before it is over. */
    rows->gauge->Pulse();
    RefreshElapsed();
}

void CaptureWizard::OnRestartAsAdministrator(wxCommandEvent&)
{
    wchar_t exe[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0)
    {
        return;
    }

    if (::ShellExecuteW(nullptr, L"runas", exe, nullptr, nullptr, SW_SHOWNORMAL) <= reinterpret_cast<HINSTANCE>(32))
    {
        wxMessageBox("AppBox could not be restarted as an administrator.\n\n"
                     "Start it as an administrator yourself and try the capture again.",
                     "Capture", wxOK | wxICON_ERROR, this);
        return;
    }

    /*
     * The elevated copy opens as a session of its own, so the window of this
     * session goes away with the dialog.
     */
    Show(false);
    if (auto* owner = GetParent())
    {
        owner->Close(true);
    }
}

void CaptureWizard::OnModePicked(wxCommandEvent&)
{
    const int selection = mode_group_->Selection();
    if (selection < 0)
    {
        return;
    }

    if (!model_.SelectMode(static_cast<appbox::CaptureMode>(selection)))
    {
        /* The mode is not offered, so the group follows the model again. */
        mode_group_->SetSelection(static_cast<int>(model_.Mode()));
        return;
    }

    /* The prerequisites, the pages and the buttons follow the new mode. */
    RefreshStage();
}

void CaptureWizard::OnBaselinePicked(wxCommandEvent&)
{
    const int selection = baseline_group_->Selection();
    if (selection < 0)
    {
        return;
    }

    /*
     * The source is only stored here: the work of the source is asked for on
     * the step which picks the file, so switching the option neither opens a
     * dialog nor starts a run.
     */
    if (!model_.SelectBaselineSource(static_cast<appbox::SnapshotBaselineSource>(selection)))
    {
        /* The source is not offered, so the group follows the model again. */
        baseline_group_->SetSelection(static_cast<int>(model_.BaselineSource()));
        return;
    }

    RefreshStage();
}

void CaptureWizard::OnSnapshotPathChanged(wxCommandEvent&)
{
    /*
     * The path is the state of the model alone: only the button which leaves
     * the step and the state below the control follow it, so neither the tree
     * nor the description is rebuilt while the user types.
     */
    model_.SetSnapshotPath(snapshot_path_text_->GetValue().ToStdWstring());

    advance_button_->Enable(model_.CanAdvance());
    SetWrappedLabel(baseline_text_, BaselineStatusText(model_), baseline_text_value_);
}

void CaptureWizard::OnBrowseSnapshotPath(wxCommandEvent&)
{
    if (browse_handler_ == nullptr)
    {
        return;
    }

    std::wstring path = model_.SnapshotPath();
    if (!browse_handler_(model_.BaselineSource(), path))
    {
        /* A dialog which was dismissed leaves the path the user set as it is. */
        return;
    }

    model_.SetSnapshotPath(path);
    RefreshStage();
}

void CaptureWizard::OnComparisonRequested(wxCommandEvent&)
{
    if (stage_handler_ == nullptr || model_.Stage() != appbox::CaptureWizardStage::Comparison)
    {
        return;
    }

    /* The offer is over while the new run is on its way. */
    comparison_failed_ = false;
    RefreshStage();

    stage_handler_(appbox::CaptureWizardStage::Comparison, appbox::CaptureWizardStage::Comparison);
}

void CaptureWizard::OnInstallationDoneToggled(wxCommandEvent& event)
{
    model_.SetInstallationDone(event.IsChecked());

    /*
     * The confirmation is what enables the button which leaves the step, so
     * the one place which refreshes the stage follows the box as well.
     */
    RefreshStage();
}

void CaptureWizard::RefreshElapsed()
{
    const auto* rows = RowsOf(model_.Stage());
    if (rows == nullptr || rows->elapsed == nullptr)
    {
        return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_);
    SetLabelIfChanged(rows->elapsed, wxString::FromUTF8(appbox::FormatElapsed(elapsed)));
}
