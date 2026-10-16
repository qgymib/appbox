#include "ActionDialog.hpp"
#include "core/BuildReport.hpp"
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>

namespace
{

/** Border of the dialog and gap between its parts. */
constexpr int kBorder = 12;

/** Row and column gap of the grid below the separator. */
constexpr int kRowGap = 6;

/** Column gap of the grid below the separator. */
constexpr int kColumnGap = 12;

/**
 * @brief Width the progress bar reserves for itself.
 *
 * The values of the grid start out empty, so the bar is the only part of the
 * dialog which tells how wide a value can become: it sets the width the whole
 * dialog is fitted from and keeps it stable while the values change.
 *
 * The width is shared by every action which reports through this dialog, so
 * the captures and the builds are as wide as each other. The value is chosen
 * for the object row: a path which the bar leaves room for is shortened far
 * less often, while the dialog stays narrower than the workspace of the main
 * window. The dialog ends up at `kGaugeMinWidth + 2 * kBorder` pixels wide.
 */
constexpr int kGaugeMinWidth = 560;

/**
 * @brief Replace the label of a static text only when it changed.
 *
 * A label which is set again is a repaint without a reason, and the values
 * of the rows are rewritten on every applied update of the dialog.
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
 * @brief Map the buttons of the dialog onto the flags of the standard button
 *        sizer of wxWidgets.
 *
 * @param[in] buttons Buttons the dialog shows.
 * @return Flags for wxDialog::CreateButtonSizer().
 */
long MapButtonFlags(long buttons)
{
    long flags = 0;
    if ((buttons & ActionDialog::Buttons_Ok) != 0)
    {
        flags |= wxOK;
    }
    if ((buttons & ActionDialog::Buttons_Cancel) != 0)
    {
        flags |= wxCANCEL;
    }
    if ((buttons & ActionDialog::Buttons_Close) != 0)
    {
        flags |= wxCLOSE;
    }
    if ((buttons & ActionDialog::Buttons_Yes) != 0)
    {
        flags |= wxYES;
    }
    if ((buttons & ActionDialog::Buttons_No) != 0)
    {
        flags |= wxNO;
    }
    return flags;
}

} // namespace

ActionDialog::ActionDialog(wxWindow* parent, const wxString& title, const Options& options)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), mode_(options.mode),
      throttle_(options.throttle_interval), pending_fields_(options.field_labels.size()),
      start_(std::chrono::steady_clock::now())
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /* The description is the only text which can span several lines. */
    auto* description = new wxStaticText(this, wxID_ANY, options.description);
    sizer->Add(description, 0, wxEXPAND | wxALL, kBorder);

    gauge_ = new wxGauge(this, wxID_ANY, 100);
    gauge_->SetMinSize(wxSize(kGaugeMinWidth, -1));
    sizer->Add(gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    /*
     * The labels share one column, so the values start below each other; the
     * value column grows with the dialog, which is what keeps the shortening
     * of a value in the middle instead of growing the dialog.
     */
    auto* grid = new wxFlexGridSizer(2, kRowGap, kColumnGap);
    grid->AddGrowableCol(1, 1);

    action_text_ = AddGridRow(*grid, "Action:");
    object_text_ = AddGridRow(*grid, "Object:");
    for (const auto& label : options.field_labels)
    {
        field_texts_.push_back(AddGridRow(*grid, label));
    }
    elapsed_text_ = AddGridRow(*grid, "Elapsed:");

    sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

    if (options.buttons != Buttons_None)
    {
        if (auto* buttons = CreateButtonSizer(MapButtonFlags(options.buttons)))
        {
            sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kBorder);

            Bind(wxEVT_BUTTON, &ActionDialog::OnButton, this);
        }
    }

    Bind(wxEVT_CLOSE_WINDOW, &ActionDialog::OnClose, this);

    SetSizer(sizer);

    /*
     * The dialog is fitted once from the parts which never change and locks
     * that size: no later value can move the buttons or any other part again.
     */
    Fit();
    const wxSize size = GetSize();
    SetMinSize(size);
    SetMaxSize(size);

    RefreshElapsed();

    /*
     * One timer drives everything which has to go on without an update: the
     * cycle of the indeterminate bar, the elapsed time and the newest of the
     * updates which arrived inside the throttle interval.
     */
    const auto tick = options.throttle_interval > std::chrono::milliseconds::zero() ? options.throttle_interval
                                                                                    : std::chrono::milliseconds(100);
    tick_timer_ = new wxTimer(this);
    Bind(wxEVT_TIMER, &ActionDialog::OnTick, this, tick_timer_->GetId());
    tick_timer_->Start(static_cast<int>(tick.count()));
}

ActionDialog::~ActionDialog()
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

bool ActionDialog::Show(bool show)
{
    if (show && !IsShown())
    {
        disabler_ = std::make_unique<wxWindowDisabler>(this);
    }

    const bool changed = wxDialog::Show(show);

    if (!show)
    {
        disabler_.reset();
        if (tick_timer_ != nullptr && tick_timer_->IsRunning())
        {
            tick_timer_->Stop();
        }
    }

    return changed;
}

void ActionDialog::SetAction(const wxString& text)
{
    if (completed_ || cancelled_)
    {
        return;
    }

    pending_action_ = text;
    has_pending_ = true;

    if (throttle_.TryAcquire())
    {
        ApplyPending();
    }
}

void ActionDialog::SetObject(const wxString& text)
{
    if (completed_ || cancelled_)
    {
        return;
    }

    pending_object_ = text;
    has_pending_ = true;

    if (throttle_.TryAcquire())
    {
        ApplyPending();
    }
}

void ActionDialog::SetField(std::size_t index, const wxString& text)
{
    wxCHECK_RET(index < pending_fields_.size(), "the fields of the dialog are fixed at creation");
    if (completed_ || cancelled_)
    {
        return;
    }

    pending_fields_[index] = text;
    has_pending_ = true;

    if (throttle_.TryAcquire())
    {
        ApplyPending();
    }
}

void ActionDialog::SetProgress(int value, int range)
{
    if (completed_ || cancelled_ || mode_ != ProgressMode::Determinate)
    {
        return;
    }

    pending_value_ = value > 0 ? value : 0;
    pending_range_ = range > 0 ? range : 1;
    has_pending_ = true;

    if (throttle_.TryAcquire())
    {
        ApplyPending();
    }
}

void ActionDialog::Complete()
{
    if (completed_)
    {
        return;
    }

    completed_ = true;

    if (tick_timer_ != nullptr && tick_timer_->IsRunning())
    {
        tick_timer_->Stop();
    }

    /* The final state is a terminal update, it bypasses the throttle. */
    ApplyPending();
    RefreshElapsed();
    PresentCompletion();

    /* The dialog is shown again when the user dismissed it before. */
    Show(true);

    /*
     * The wait for the user is the modal completion state of the progress
     * dialog of wxWidgets: a new event loop keeps the application alive while
     * the call blocks the owner which reports the result.
     */
    (void)ShowModal();
}

bool ActionDialog::WasCancelled() const
{
    return cancelled_;
}

void ActionDialog::OnTick(wxTimerEvent&)
{
    if (completed_)
    {
        return;
    }

    if (mode_ == ProgressMode::Indeterminate)
    {
        gauge_->Pulse();
    }

    RefreshElapsed();

    if (has_pending_)
    {
        /*
         * The tick applies the newest of the updates which arrived inside the
         * interval, and it restarts the interval: without this, an update
         * which arrives right after the tick would pass the gate immediately
         * and two updates would be applied within milliseconds.
         */
        ApplyPending();
        (void)throttle_.TryAcquire();
    }
}

void ActionDialog::OnButton(wxCommandEvent& event)
{
    if (completed_)
    {
        EndModal(event.GetId());
        return;
    }

    /* Every button of a running action asks the owner to abort it. */
    cancelled_ = true;
    Show(false);
}

void ActionDialog::OnClose(wxCloseEvent& event)
{
    if (completed_)
    {
        EndModal(wxID_CLOSE);
        return;
    }

    /* The close box of a running action asks the owner to abort it. */
    cancelled_ = true;
    event.Skip(false);
    Show(false);
}

void ActionDialog::ApplyPending()
{
    SetLabelIfChanged(action_text_, pending_action_);
    SetLabelIfChanged(object_text_, pending_object_);
    for (std::size_t index = 0; index < field_texts_.size(); ++index)
    {
        SetLabelIfChanged(field_texts_[index], pending_fields_[index]);
    }

    if (gauge_->GetRange() != pending_range_)
    {
        gauge_->SetRange(pending_range_);
    }
    if (gauge_->GetValue() != pending_value_)
    {
        gauge_->SetValue(pending_value_);
    }

    has_pending_ = false;
}

void ActionDialog::RefreshElapsed()
{
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_);
    SetLabelIfChanged(elapsed_text_, wxString::FromUTF8(appbox::FormatElapsed(elapsed)));
}

void ActionDialog::PresentCompletion()
{
    /*
     * The cancel button becomes the close button of the result, unless the
     * dialog has a close button of its own; every other button is disabled.
     */
    auto* close = dynamic_cast<wxButton*>(FindWindowById(wxID_CLOSE));
    if (close == nullptr)
    {
        if (auto* cancel = dynamic_cast<wxButton*>(FindWindowById(wxID_CANCEL)))
        {
            cancel->SetLabel("Close");
            close = cancel;
        }
    }

    for (const auto id : { wxID_OK, wxID_YES, wxID_NO, wxID_CANCEL, wxID_CLOSE })
    {
        if (auto* button = dynamic_cast<wxButton*>(FindWindowById(id)))
        {
            if (button != close)
            {
                button->Disable();
            }
        }
    }
}

wxStaticText* ActionDialog::AddGridRow(wxFlexGridSizer& grid, const wxString& label)
{
    auto* label_text = new wxStaticText(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    /*
     * The middle ellipsis only works together with wxST_NO_AUTORESIZE: without
     * it every SetLabel() grows the control to the full width of the text,
     * which pushes it past the size the dialog locked at creation, and the
     * shortening never happens. With it the control keeps the width the sizer
     * gave the column and shortens the text into that width.
     */
    auto* value_text = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                        wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);

    grid.Add(label_text, 0, wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL);
    grid.Add(value_text, 1, wxEXPAND | wxALIGN_CENTRE_VERTICAL);

    return value_text;
}
