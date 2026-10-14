#ifndef APPBOX_PACKER_WIDGET_ACTION_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_ACTION_DIALOG_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include "core/ActionThrottle.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

class wxCloseEvent;
class wxCommandEvent;
class wxFlexGridSizer;
class wxGauge;
class wxStaticText;
class wxTimer;
class wxTimerEvent;
class wxWindowDisabler;

/**
 * @brief Dialog which reports one long action with a fixed layout.
 *
 * The dialog replaces the progress dialog of wxWidgets for the runs the main
 * window reports: the native task dialog keeps the size it was created with,
 * so a message which is longer than the ones before it pushes the buttons out
 * of the visible area, and the workarounds of the window grew with every run.
 * This dialog fits itself once from the parts which never change and locks
 * that size, so no later message can move anything again.
 *
 * The layout from top to bottom:
 *
 *  - the description of the action, a multi line text which never changes;
 *  - the progress bar, either with a value and a range or in a cycle which
 *    never ends;
 *  - a separator;
 *  - a two column grid of the rows `Action:`, `Object:`, one row per custom
 *    field and the `Elapsed:` row which the dialog keeps on its own.
 *
 * Every value of the grid is a single line; a value which does not fit into
 * its column is shortened in the middle, so the start and the end of a path
 * stay readable. The custom fields are part of the creation settings and can
 * neither be added nor removed while the dialog exists.
 *
 * The window which owns the run keeps the event loop: it shows the dialog
 * with Show() and feeds it through the setters below. The setters may be
 * called far more often than a user can read, so the dialog applies at most
 * one update per throttle interval and keeps only the newest value of the
 * updates in between; the elapsed time and the cycle of the indeterminate
 * bar advance on the same clock. All methods must be called from the main
 * thread, the throttle is about the rate of the updates and not about thread
 * safety.
 *
 * A dialog of a running action dismisses on every button and on the close
 * box of its title bar, and WasCancelled() reports that the user asked to
 * abort. Complete() presents the final state instead: the elapsed time
 * freezes, the buttons reduce to a single close button and the call returns
 * only after the user dismissed the dialog.
 */
class ActionDialog : public wxDialog
{
public:
    /**
     * @brief Mode of the progress bar.
     */
    enum class ProgressMode
    {
        /**
         * @brief The bar is set from a value and a range.
         */
        Determinate,

        /**
         * @brief The bar advances in a cycle which never ends, for a run
         *        whose total is not known before it is over.
         */
        Indeterminate,
    };

    /**
     * @brief Buttons the dialog shows, combined with the bitwise or.
     */
    enum Button
    {
        Buttons_None = 0x00,   ///< No button at all.
        Buttons_Ok = 0x01,     ///< The `OK` button.
        Buttons_Cancel = 0x02, ///< The `Cancel` button, which the result state renames to `Close`.
        Buttons_Close = 0x04,  ///< The `Close` button.
        Buttons_Yes = 0x08,    ///< The `Yes` button.
        Buttons_No = 0x10,     ///< The `No` button.
    };

    /**
     * @brief Settings of the dialog, all of them fixed at creation.
     */
    struct Options
    {
        /**
         * @brief Description of the action, shown above the progress bar.
         *
         * The text may span several lines; the caller breaks the lines
         * because the dialog does not wrap.
         */
        wxString description;

        /**
         * @brief Mode of the progress bar.
         */
        ProgressMode mode = ProgressMode::Determinate;

        /**
         * @brief Labels of the custom fields between the object row and the
         *        elapsed row.
         *
         * Every label is shown as it is, so it carries its own colon.
         */
        std::vector<wxString> field_labels;

        /**
         * @brief Buttons the dialog shows.
         *
         * While the action runs, every button dismisses the dialog and
         * counts as a cancel.
         */
        long buttons = Buttons_Cancel;

        /**
         * @brief Time between two updates the dialog applies.
         *
         * The first update of a fresh dialog passes immediately, every
         * further one which arrives inside the interval is kept and applied
         * with the newest value when the interval is over. A non positive
         * interval lets every update pass.
         */
        std::chrono::milliseconds throttle_interval{ 100 };
    };

    /**
     * @brief Create the dialog.
     *
     * The dialog fits itself from the parts which never change and locks
     * that size, so a caller cannot and does not have to size it.
     *
     * @param[in] parent Window the dialog belongs to.
     * @param[in] title Title of the dialog.
     * @param[in] options Settings of the dialog.
     */
    ActionDialog(wxWindow* parent, const wxString& title, const Options& options);

    /**
     * @brief Stop the timer of the dialog and give the windows back.
     */
    ~ActionDialog() override;

    /**
     * @brief Show or hide the dialog.
     *
     * While the dialog is shown, every other window of the application is
     * disabled, so the user cannot work around the action it reports.
     *
     * @param[in] show Whether the dialog is shown.
     * @return Whether the state changed.
     */
    bool Show(bool show = true) override;

    /**
     * @brief Set the text of the action row.
     * @param[in] text Text, a single line.
     */
    void SetAction(const wxString& text);

    /**
     * @brief Set the text of the object row, e.g. the entry read right now.
     * @param[in] text Text, a single line.
     */
    void SetObject(const wxString& text);

    /**
     * @brief Set the value of a custom field.
     *
     * @param[in] index Field to set, an index of the labels of the options.
     * @param[in] text Text, a single line.
     */
    void SetField(std::size_t index, const wxString& text);

    /**
     * @brief Set the progress bar.
     *
     * The call is ignored by an indeterminate bar, whose cycle the dialog
     * advances on its own clock.
     *
     * @param[in] value Value of the bar.
     * @param[in] range Maximum of the bar, a non positive range is kept at 1.
     */
    void SetProgress(int value, int range);

    /**
     * @brief Present the final state and wait for the user.
     *
     * The pending updates are applied at once, the elapsed time freezes, the
     * buttons reduce to a single close button and the dialog is shown again
     * if the user dismissed it before. The call returns when the user closed
     * the dialog, so the owner can destroy it right after.
     */
    void Complete();

    /**
     * @brief Tell whether the user dismissed the dialog while the action was
     *        running.
     * @return true after a button or the close box dismissed the dialog
     *         before Complete() presented the final state.
     */
    bool WasCancelled() const;

private:
    /**
     * @brief Advance the bar and the clock and apply the pending updates.
     * @param[in] event Timer event of the tick timer.
     */
    void OnTick(wxTimerEvent& event);

    /**
     * @brief Dismiss the dialog.
     * @param[in] event Button event of one of the buttons of the dialog.
     */
    void OnButton(wxCommandEvent& event);

    /**
     * @brief Dismiss the dialog through the close box of the title bar.
     * @param[in] event Close event.
     */
    void OnClose(wxCloseEvent& event);

    /**
     * @brief Apply the pending updates to the controls.
     *
     * A value which did not change since the last application skips its
     * control, so the dialog does not repaint without a reason.
     */
    void ApplyPending();

    /**
     * @brief Show the time which passed since the dialog was created.
     */
    void RefreshElapsed();

    /**
     * @brief Reduce the buttons to the single close button of the result.
     */
    void PresentCompletion();

    /**
     * @brief Add one label and value row to the grid of the dialog.
     *
     * @param[in,out] grid Grid the row is added to.
     * @param[in] label Label of the row, shown right aligned.
     * @return The value control of the row.
     */
    wxStaticText* AddGridRow(wxFlexGridSizer& grid, const wxString& label);

    /**
     * @brief Mode of the progress bar.
     */
    ProgressMode mode_;

    /**
     * @brief Gate which decides whether an update may be applied right now.
     */
    appbox::ActionThrottle throttle_;

    /**
     * @brief Time the run started, the dialog measures the elapsed time from
     *        it.
     */
    std::chrono::steady_clock::time_point start_;

    /**
     * @brief Progress bar of the dialog.
     */
    wxGauge* gauge_ = nullptr;

    /**
     * @brief Value control of the action row.
     */
    wxStaticText* action_text_ = nullptr;

    /**
     * @brief Value control of the object row.
     */
    wxStaticText* object_text_ = nullptr;

    /**
     * @brief Value controls of the custom fields, one per field label.
     */
    std::vector<wxStaticText*> field_texts_;

    /**
     * @brief Value control of the elapsed row.
     */
    wxStaticText* elapsed_text_ = nullptr;

    /**
     * @brief Timer which advances the bar and the clock and applies the
     *        pending updates.
     */
    wxTimer* tick_timer_ = nullptr;

    /**
     * @brief Newest action text which was not applied yet.
     */
    wxString pending_action_;

    /**
     * @brief Newest object text which was not applied yet.
     */
    wxString pending_object_;

    /**
     * @brief Newest field texts which were not applied yet.
     */
    std::vector<wxString> pending_fields_;

    /**
     * @brief Newest value of the bar which was not applied yet.
     */
    int pending_value_ = 0;

    /**
     * @brief Newest maximum of the bar which was not applied yet.
     */
    int pending_range_ = 1;

    /**
     * @brief Whether an update waits for the next tick of the timer.
     */
    bool has_pending_ = false;

    /**
     * @brief Whether the user dismissed the dialog while the action ran.
     */
    bool cancelled_ = false;

    /**
     * @brief Whether Complete() presented the final state already.
     */
    bool completed_ = false;

    /**
     * @brief Disabler which keeps the user away from the other windows while
     *        the dialog is shown.
     */
    std::unique_ptr<wxWindowDisabler> disabler_;
};

#endif // APPBOX_PACKER_WIDGET_ACTION_DIALOG_HPP
