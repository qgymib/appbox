#ifndef APPBOX_PACKER_WIDGET_SETTINGS_PANEL_HPP
#define APPBOX_PACKER_WIDGET_SETTINGS_PANEL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/combobox.h>
#include "core/ProjectType.hpp"

/** Command id of the archive path box of the Output tab. */
extern const int kSettingsOutputPath;

/** Command id of the "Browse..." button of the Output tab. */
extern const int kSettingsBrowseOutput;

/** Command id of the project type box of the Output tab. */
extern const int kSettingsProjectType;

class TabBar;
class wxSimplebook;

/**
 * @brief Settings workspace of the packer.
 *
 * The workspace holds a tab strip above the page of the selected tab. The
 * `Output` tab carries the destination of the pack run: the archive path with
 * the `Browse...` button which picks it, and the project type which decides
 * what the `Build` command writes. The strip is shared with the other
 * workspaces, so a further tab is added by appending to the strip and to the
 * book.
 *
 * The workspace holds no model of its own: the session of the frame owns the
 * output path and the project type, pushes them into the controls through
 * SetOutputPath() and SetProjectType(), and reads them back through
 * GetOutputPath() and GetProjectType(). The controls raise their command
 * events without a handler of their own, so the frame handles them the same
 * way it handles every other workspace.
 *
 * The options of the tab are laid out in one grid, so the labels line up in
 * one column and the controls in the next one. The archive path box offers the
 * whole path as its tooltip while the box is too narrow to show it, which is
 * the only way to read a path the box cuts off.
 */
class SettingsPanel : public wxPanel
{
public:
    /**
     * @brief Create the settings workspace.
     * @param[in] parent Parent window.
     */
    explicit SettingsPanel(wxWindow* parent);

    /**
     * @brief Get the archive path the Output tab shows.
     * @return The archive path, empty when none was chosen.
     */
    wxString GetOutputPath() const;

    /**
     * @brief Set the archive path the Output tab shows.
     *
     * The box is changed without raising a command event, so a caller which
     * restores a configuration does not re-enter its own handler.
     *
     * @param[in] path Archive path.
     */
    void SetOutputPath(const wxString& path);

    /**
     * @brief Get the project type the Output tab shows.
     * @return The selected project type, `Standalone` when the box shows no
     *         selection.
     */
    appbox::ProjectType GetProjectType() const;

    /**
     * @brief Select a project type in the Output tab.
     *
     * The box is changed without raising a command event, so a caller which
     * restores a configuration does not re-enter its own handler.
     *
     * @param[in] type Project type to show.
     */
    void SetProjectType(appbox::ProjectType type);

private:
    /**
     * @brief Create the page of the `Output` tab.
     * @param[in] parent Parent window of the page.
     * @return The created page.
     */
    wxWindow* CreateOutputPage(wxWindow* parent);

    /**
     * @brief Show the page of the activated tab.
     * @param[in] event Command event carrying the tab index.
     */
    void OnTabChanged(wxCommandEvent& event);

    /**
     * @brief Update the tooltip of the archive path box while the cursor is over it.
     *
     * The full path is offered while the box cannot show it in full, and no
     * tooltip is offered while the whole path is visible.
     *
     * @param[in] event Mouse event of the box.
     */
    void OnOutputPathMotion(wxMouseEvent& event);

    /**
     * @brief Drop the tooltip of the archive path box while the cursor leaves it.
     * @param[in] event Mouse event of the box.
     */
    void OnOutputPathLeave(wxMouseEvent& event);

    /**
     * @brief Update the tooltip of the archive path box.
     */
    void UpdateOutputPathToolTip();

    /**
     * @brief Tell whether the archive path is wider than the box which shows it.
     * @param[in] path Path shown by the box.
     * @return true while the box cannot show the whole path.
     */
    bool IsOutputPathTruncated(const wxString& path);

    /**
     * @brief Set the tooltip of the archive path box.
     *
     * The text is only written while it differs from the text the box already
     * carries, because writing the tooltip again restarts the delay after which
     * wxWidgets pops it up.
     *
     * @param[in] text Text to show, empty to drop the tooltip.
     */
    void SetOutputPathToolTip(const wxString& text);

    TabBar*       tab_bar_ = nullptr;
    wxSimplebook* pages_ = nullptr;

    /**
     * @brief The `Output File` box of the Output tab.
     */
    wxTextCtrl* output_path_ = nullptr;

    /**
     * @brief The `Project Type` box of the Output tab.
     *
     * The box lists the project types in the order of ProjectTypeAt(), so the
     * selection is the index of the type it shows.
     */
    wxComboBox* project_type_ = nullptr;

    /**
     * @brief Text the archive path box currently shows as its tooltip.
     *
     * Empty while the box carries no tooltip, which is the case while the box
     * shows the whole path.
     */
    wxString output_path_tooltip_;
};

#endif // APPBOX_PACKER_WIDGET_SETTINGS_PANEL_HPP
