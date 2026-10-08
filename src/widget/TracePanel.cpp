#include "TracePanel.hpp"
#include "TracerListModel.hpp"
#include "core/TracerModel.hpp"
#include "WString.hpp"
#include "tracer/TargetProgram.hpp"
#include "tracer/TracedModules.hpp"
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <windows.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

wxDEFINE_EVENT(APPBOX_TRACER_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(APPBOX_TRACER_FINISHED, wxThreadEvent);

namespace
{

/** Border of the form of the workspace. */
constexpr int kFormBorder = 12;

/** Gap between two rows of the form. */
constexpr int kRowGap = 12;

/** Gap between two columns of the form, which is the gap between a label and its control. */
constexpr int kFieldGap = 8;

/** Minimum width of the view box of the form. */
constexpr int kFieldWidth = 260;

/** Width of the function column of the list. */
constexpr int kFunctionWidth = 520;

/** Rows of the form of the workspace. */
constexpr int kFormRows = 3;

/** Columns of the form: the labels and the controls. */
constexpr int kFormColumns = 2;

/** Column of the form which holds the controls; the column takes the spare width. */
constexpr int kControlColumn = 1;

/** Colour of the status line and of the hint texts. */
const wxColour kHintColour(0x5A, 0x5A, 0x5A);

/**
 * @brief Split the text of the arguments box the way Windows splits a command line.
 *
 * @param[in] text Text of the arguments box.
 * @return The arguments; empty for an empty or blank text.
 */
std::vector<std::wstring> SplitArguments(const wxString& text)
{
    const std::wstring command = L"appbox-tracer " + text.ToStdWstring();

    int     count = 0;
    LPWSTR* list = ::CommandLineToArgvW(command.c_str(), &count);
    if (list == nullptr)
    {
        return {};
    }

    std::vector<std::wstring> arguments;
    for (int index = 1; index < count; ++index)
    {
        arguments.emplace_back(list[index]);
    }

    ::LocalFree(list);
    return arguments;
}

} // namespace

TracePanel::TracePanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    target_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    target_->SetToolTip("Program the tracer runs below the debugger");
    target_->Bind(wxEVT_KILL_FOCUS, &TracePanel::OnTargetChanged, this);
    target_->Bind(wxEVT_TEXT_ENTER, &TracePanel::OnTargetChanged, this);

    browse_ = new wxButton(this, wxID_ANY, "Browse...");
    browse_->SetToolTip("Choose the program to trace");
    browse_->Bind(wxEVT_BUTTON, &TracePanel::OnBrowse, this);

    arguments_ = new wxTextCtrl(this, wxID_ANY);
    arguments_->SetToolTip("Arguments passed to the program, quoted as on a command line");

    wxArrayString views;
    views.Add(appbox::TracerViewDisplayName(appbox::TracerView::Scope));
    views.Add(appbox::TracerViewDisplayName(appbox::TracerView::AllExports));

    view_ = new wxComboBox(this, wxID_ANY, appbox::TracerViewDisplayName(appbox::TracerView::Scope), wxDefaultPosition,
                           wxDefaultSize, views, wxCB_READONLY);
    view_->SetMinSize(wxSize(kFieldWidth, -1));
    view_->SetToolTip("The isolation entry points are the functions an isolation layer has to intercept; the all "
                      "exports view lists every executable export parsed from ntdll, kernel32, kernelbase, ws2_32 and "
                      "dnsapi");
    view_->Bind(wxEVT_COMBOBOX, &TracePanel::OnViewChanged, this);

    run_ = new wxButton(this, wxID_ANY, "Run");
    run_->SetToolTip("Run the target below the debugger");
    run_->Bind(wxEVT_BUTTON, &TracePanel::OnRun, this);

    export_ = new wxButton(this, wxID_ANY, "Export JSON...");
    export_->SetToolTip("Write the functions the run used to a JSON file");
    export_->Bind(wxEVT_BUTTON, &TracePanel::OnExport, this);

    model_ = new TracerListModel();
    list_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
    list_->AssociateModel(model_);

    /* AssociateModel() adds a reference which the control releases again. */
    model_->DecRef();

    list_->AppendTextColumn("Function", TracerListModel::FunctionColumn, wxDATAVIEW_CELL_INERT, kFunctionWidth,
                            wxALIGN_LEFT);

    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    status_->SetForegroundColour(kHintColour);

    /*
     * The first column holds the labels and the second one the controls, so
     * every box starts at the same left edge while the second column takes the
     * spare width of the page.
     */
    auto* target_cell = new wxBoxSizer(wxHORIZONTAL);
    target_cell->Add(target_, 1, wxALIGN_CENTER_VERTICAL);
    target_cell->Add(browse_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, kFieldGap);

    auto* grid = new wxFlexGridSizer(kFormRows, kFormColumns, kRowGap, kFieldGap);
    grid->AddGrowableCol(kControlColumn, 1);
    grid->Add(new wxStaticText(this, wxID_ANY, "Target Program:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(target_cell, 1, wxEXPAND);
    grid->Add(new wxStaticText(this, wxID_ANY, "Arguments:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(arguments_, 1, wxEXPAND);
    grid->Add(new wxStaticText(this, wxID_ANY, "View:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(view_, 1, wxEXPAND);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(run_, 0, wxALIGN_CENTER_VERTICAL);
    buttons->Add(export_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, kFieldGap);
    buttons->AddStretchSpacer();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kFormBorder);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kFormBorder);
    sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kFormBorder);
    sizer->Add(status_, 0, wxEXPAND | wxALL, kFormBorder);
    SetSizer(sizer);

    Bind(APPBOX_TRACER_PROGRESS, &TracePanel::OnTraceProgress, this);
    Bind(APPBOX_TRACER_FINISHED, &TracePanel::OnTraceFinished, this);

    SetStatus("Choose the target program to trace.");
    UpdateButtons();
}

TracePanel::~TracePanel()
{
    if (thread_.joinable())
    {
        appbox::tracer::RequestTraceInterrupt();
        thread_.join();
    }
}

appbox::TracerView TracePanel::CurrentView() const
{
    return view_->GetSelection() == 1 ? appbox::TracerView::AllExports : appbox::TracerView::Scope;
}

void TracePanel::ReloadView()
{
    entries_.clear();
    result_ = appbox::tracer::TraceResult();
    has_result_ = false;
    target_path_.clear();
    loaded_target_.clear();
    module_directory_.clear();
    model_->SetEntries(std::vector<appbox::TracerEntry>());

    const wxString text = target_->GetValue().Trim().Trim(false);
    if (text.empty())
    {
        SetStatus("Choose the target program to trace.");
        UpdateButtons();
        return;
    }

    const std::filesystem::path target = appbox::tracer::ResolveTargetProgram(text.ToStdWstring());
    if (target.empty())
    {
        SetStatus(wxString("The program '") + text + "' was not found.");
        UpdateButtons();
        return;
    }

    target_path_ = target.wstring();
    loaded_target_ = target_path_;
    module_directory_ = appbox::tracer::ModuleDirectoryForTarget(target);

    std::vector<appbox::TracerEntry> entries;
    std::string                      error;
    if (!appbox::BuildTracerView(CurrentView(), module_directory_, entries, error))
    {
        SetStatus(wxString::FromUTF8(error));
        UpdateButtons();
        return;
    }

    entries_ = std::move(entries);
    model_->SetEntries(entries_);
    SetStatus(wxString::FromUTF8(std::to_string(entries_.size()) + " functions in the " +
                                 appbox::TracerViewDisplayName(CurrentView()) +
                                 " view. Run the program to mark the ones it uses."));
    UpdateButtons();
}

void TracePanel::OnBrowse(wxCommandEvent&)
{
    wxFileDialog dialog(this, "Target Program", wxEmptyString, wxEmptyString,
                        "Programs (*.exe)|*.exe|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    target_->ChangeValue(dialog.GetPath());
    ReloadView();
}

void TracePanel::OnTargetChanged(wxEvent& event)
{
    if (target_->GetValue().ToStdWstring() != loaded_target_)
    {
        ReloadView();
    }

    event.Skip();
}

void TracePanel::OnViewChanged(wxCommandEvent& event)
{
    ReloadView();
    event.Skip();
}

void TracePanel::OnRun(wxCommandEvent&)
{
    if (thread_.joinable())
    {
        /* The button is the Stop button while a run is going on. */
        appbox::tracer::RequestTraceInterrupt();
        SetStatus("Stopping the run...");
        return;
    }

    if (entries_.empty())
    {
        SetStatus("Choose a valid target program first.");
        return;
    }

    const appbox::TracerView view = CurrentView();
    if (view == appbox::TracerView::AllExports &&
        wxMessageBox("The all exports view arms one breakpoint per export of the traced modules "
                     "(several thousand); arming them takes minutes before the program even starts.\n\n"
                     "Continue?",
                     "Tracer", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
    {
        return;
    }

    const std::filesystem::path     target = std::filesystem::path(target_path_);
    const std::vector<std::wstring> arguments = SplitArguments(arguments_->GetValue());
    const std::filesystem::path     directory = module_directory_;

    SetRunning(true);
    SetStatus(wxString("Running ") + target_path_ + "...");

    thread_ = std::thread([this, target, arguments, view, directory]() {
        const auto report = [this](const std::wstring& line) {
            auto* event = new wxThreadEvent(APPBOX_TRACER_PROGRESS);
            event->SetPayload(line);
            this->GetEventHandler()->QueueEvent(event);
        };

        auto* event = new wxThreadEvent(APPBOX_TRACER_FINISHED);
        event->SetPayload(appbox::RunTracerSession(target, arguments, view, directory, report));
        this->GetEventHandler()->QueueEvent(event);
    });
}

void TracePanel::OnExport(wxCommandEvent&)
{
    const wxString target_text(target_path_);
    wxFileName     suggested(target_text);
    suggested.SetExt("json");

    wxFileDialog dialog(this, "Export Tracer Result", suggested.GetPath(), suggested.GetFullName(),
                        "JSON file (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!appbox::SaveTracerExport(dialog.GetPath().ToStdWstring(), target_path_, CurrentView(), result_, entries_,
                                  error))
    {
        spdlog::error("the tracer export failed: {}", error);
        wxMessageBox("The result could not be exported:\n\n" + wxString::FromUTF8(error), "Export Tracer Result",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    SetStatus(wxString("Result exported to ") + dialog.GetPath());
}

void TracePanel::OnTraceProgress(wxThreadEvent& event)
{
    if (thread_.joinable())
    {
        SetStatus(event.GetPayload<std::wstring>());
    }
}

void TracePanel::OnTraceFinished(wxThreadEvent& event)
{
    if (thread_.joinable())
    {
        thread_.join();
    }

    const appbox::TracerRunOutcome outcome = event.GetPayload<appbox::TracerRunOutcome>();
    SetRunning(false);

    if (!outcome.error.empty())
    {
        has_result_ = false;
        SetStatus(wxString(outcome.error));
        UpdateButtons();
        return;
    }

    result_ = outcome.result;
    has_result_ = true;

    appbox::MarkTracerEntries(entries_, result_.names);
    appbox::OrderTracerEntries(entries_);
    model_->SetEntries(entries_);

    SetStatus(appbox::TracerRunSummary(result_));
    UpdateButtons();
}

void TracePanel::SetRunning(bool running)
{
    run_->SetLabel(running ? "Stop" : "Run");
    target_->Enable(!running);
    arguments_->Enable(!running);
    browse_->Enable(!running);
    view_->Enable(!running);
    UpdateButtons();
}

void TracePanel::UpdateButtons()
{
    const bool idle = !thread_.joinable();
    export_->Enable(idle && has_result_ && model_->GetCount() > 0);
}

void TracePanel::SetStatus(const wxString& text)
{
    status_->SetLabel(text);
    Layout();
}
