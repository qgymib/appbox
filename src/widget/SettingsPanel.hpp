#ifndef APPBOX_PACKER_WIDGET_SETTINGS_PANEL_HPP
#define APPBOX_PACKER_WIDGET_SETTINGS_PANEL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/combobox.h>
#include "core/ApplicationMetadata.hpp"
#include "core/ProjectType.hpp"
#include <vector>

/** Command id of the archive path box of the Output tab. */
extern const int kSettingsOutputPath;

/** Command id of the "Browse..." button of the Output tab. */
extern const int kSettingsBrowseOutput;

/** Command id of the project type box of the Output tab. */
extern const int kSettingsProjectType;

/** Command id of the inherit source box of the Metadata tab. */
extern const int kSettingsMetadataSource;

/** Command id of the "Browse..." button of the Metadata tab. */
extern const int kSettingsMetadataBrowse;

/** Command id of the field boxes of the Metadata tab. */
extern const int kSettingsMetadataField;

/** Command id of the "Customize..." button of the Metadata tab. */
extern const int kSettingsMetadataCustomize;

/**
 * @brief Event raised when the Metadata tab changes the file properties.
 *
 * The frame owns the metadata of the session, so the tab reports every edit -
 * the inherit source, a field of the tab and the result of the customization
 * dialog - through this event instead of storing a state of its own.
 */
wxDECLARE_EVENT(APPBOX_METADATA_CHANGED, wxCommandEvent);

/**
 * @brief One program the file properties of the launcher can be inherited from.
 */
struct MetadataSource
{
    wxString label; ///< Text the inherit source box shows.
    wxString path;  ///< Host path of the program, empty for the default source.
};

class TabBar;
class wxSimplebook;

/**
 * @brief Settings workspace of the packer.
 *
 * The workspace holds a tab strip above the page of the selected tab. The
 * `Output` tab carries the destination of the pack run: the archive path with
 * the `Browse...` button which picks it, and the project type which decides
 * what the `Build` command writes. The `Metadata` tab carries the file
 * properties the launcher of the archive is packed with: the program they are
 * inherited from with the `Browse...` button which picks it, the fields a user
 * fills in most of the time and the `Customize...` button which opens the
 * dialog of the remaining fields. The strip is shared with the other
 * workspaces, so a further tab is added by appending to the strip and to the
 * book.
 *
 * The workspace holds no model of its own: the session of the frame owns the
 * output path, the project type and the metadata, pushes them into the
 * controls through SetOutputPath(), SetProjectType(), SetMetadataSources() and
 * SetMetadataValues(), and reads them back through GetOutputPath(),
 * GetProjectType(), GetMetadataSource() and GetMetadataValues(). The controls
 * raise their command events without a handler of their own, so the frame
 * handles them the same way it handles every other workspace; an edit of the
 * metadata is reported through APPBOX_METADATA_CHANGED, because the tab cannot
 * tell an override from a value which follows the source program.
 *
 * The options of a tab are laid out in one grid, so the labels line up in one
 * column and the controls in the next one. The archive path box offers the
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

    /**
     * @brief Fill the inherit source box of the Metadata tab.
     *
     * The first entry of the box is the default source of the session; the
     * entries below it are the programs the tab offers. A selected path which
     * the box does not hold yet - a program which was browsed or a source of
     * an imported project - is added to the box, so the selection can always be
     * shown.
     *
     * @param[in] sources Programs the box offers, the default source first.
     * @param[in] selected Host path of the program to select, empty for the
     *                     default source.
     */
    void SetMetadataSources(const std::vector<MetadataSource>& sources, const wxString& selected);

    /**
     * @brief Fill the fields of the Metadata tab.
     *
     * The boxes are changed without raising a command event, so a caller which
     * refreshes the tab does not re-enter its own handler.
     *
     * @param[in] values Values to show; a field the list does not hold is shown
     *                   empty.
     * @param[in] note Text which explains where the values come from.
     */
    void SetMetadataValues(const std::vector<appbox::MetadataField>& values, const wxString& note);

    /**
     * @brief Get the program the Metadata tab inherits from.
     * @return Host path of the selected program, empty for the default source.
     */
    wxString GetMetadataSource() const;

    /**
     * @brief Get the values the Metadata tab shows.
     * @return The value of every field, in MetadataFields() order.
     */
    const std::vector<appbox::MetadataField>& GetMetadataValues() const;

    /**
     * @brief Enable or disable the controls of the Metadata tab.
     *
     * A patch project writes no launcher, so its file properties are not part
     * of the product and the tab cannot be edited.
     *
     * @param[in] enabled Whether the tab can be edited.
     */
    void EnableMetadata(bool enabled);

private:
    /**
     * @brief Create the page of the `Output` tab.
     * @param[in] parent Parent window of the page.
     * @return The created page.
     */
    wxWindow* CreateOutputPage(wxWindow* parent);

    /**
     * @brief Create the page of the `Metadata` tab.
     * @param[in] parent Parent window of the page.
     * @return The created page.
     */
    wxWindow* CreateMetadataPage(wxWindow* parent);

    /**
     * @brief Read the field boxes into the values the tab shows.
     */
    void CollectMetadataValues();

    /**
     * @brief Report an edit of the inherit source box.
     * @param[in] event Command event of the box.
     */
    void OnMetadataSourceChanged(wxCommandEvent& event);

    /**
     * @brief Report an edit of one field box.
     * @param[in] event Command event of the box.
     */
    void OnMetadataFieldEdited(wxCommandEvent& event);

    /**
     * @brief Open the dialog of the remaining fields and apply its result.
     * @param[in] event Command event of the button.
     */
    void OnMetadataCustomize(wxCommandEvent& event);

    /**
     * @brief Tell the frame that the Metadata tab was edited.
     */
    void ReportMetadataChanged();

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

    /**
     * @brief The page of the `Metadata` tab.
     */
    wxWindow* metadata_page_ = nullptr;

    /**
     * @brief The `Inherit From` box of the Metadata tab.
     */
    wxComboBox* metadata_source_ = nullptr;

    /**
     * @brief The note of the Metadata tab, which names the source program.
     */
    wxStaticText* metadata_note_ = nullptr;

    /**
     * @brief The field boxes of the Metadata tab, one per common field.
     */
    std::vector<wxTextCtrl*> metadata_fields_;

    /**
     * @brief Programs the inherit source box offers, in the order of the box.
     */
    std::vector<MetadataSource> metadata_sources_;

    /**
     * @brief The values the Metadata tab shows.
     *
     * The list holds one entry per field of a version resource, so the values
     * of the fields which the tab does not show a box for survive an edit of
     * one of its boxes.
     */
    std::vector<appbox::MetadataField> metadata_values_;
};

#endif // APPBOX_PACKER_WIDGET_SETTINGS_PANEL_HPP
