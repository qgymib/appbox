#ifndef APPBOX_PACKER_WIDGET_RIBBON_BAR_HPP
#define APPBOX_PACKER_WIDGET_RIBBON_BAR_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/combobox.h>
#include <wx/ribbon/bar.h>
#include <wx/ribbon/buttonbar.h>
#include <wx/ribbon/page.h>
#include <wx/ribbon/panel.h>
#include "core/ProjectType.hpp"

/** Command id of the "Startup Files" ribbon button. */
extern const int kRibbonStartupFiles;

/** Command id of the "Build" ribbon button. */
extern const int kRibbonBuild;

/** Command id of the "Build and Run" ribbon button. */
extern const int kRibbonBuildAndRun;

/** Command id of the "Browse..." button of the Output group. */
extern const int kRibbonBrowseOutput;

/** Command id of the archive path box of the Output group. */
extern const int kRibbonOutputPath;

/** Command id of the project type box of the Output group. */
extern const int kRibbonProjectType;

/**
 * @brief Ribbon toolbar of the packer.
 *
 * The bar hosts the `Home` and `Advanced` pages. `Home` carries the groups of
 * the packaging workflow: Capture and Snapshot are reserved placeholders, the
 * Build group triggers the pack run, the Startup group selects the main
 * program and the Output group owns the destination archive path.
 *
 * The bar only raises command events; every action lives in the frame.
 */
class RibbonBar : public wxRibbonBar
{
public:
    /**
     * @brief Create the ribbon bar with both pages.
     * @param[in] parent Parent window.
     * @param[in] id Window identifier.
     */
    RibbonBar(wxWindow* parent, wxWindowID id);

    /**
     * @brief Get the destination archive path of the Output group.
     * @return The archive path, empty when none was chosen.
     */
    wxString GetOutputPath() const;

    /**
     * @brief Set the destination archive path of the Output group.
     * @param[in] path Archive path.
     */
    void SetOutputPath(const wxString& path);

    /**
     * @brief Get the project type the Output group shows.
     * @return The selected project type, `Standalone` when the box shows no
     *         selection.
     */
    appbox::ProjectType GetProjectType() const;

    /**
     * @brief Select a project type in the Output group.
     *
     * The box is changed without raising a command event, so a caller which
     * restores a configuration does not re-enter its own handler.
     *
     * @param[in] type Project type to show.
     */
    void SetProjectType(appbox::ProjectType type);

    /**
     * @brief Enable or disable the "Build and Run" button.
     *
     * A patch package carries no loader, so the run command is only offered for
     * a standalone project.
     *
     * @param[in] enabled Whether the button accepts input.
     */
    void SetBuildAndRunEnabled(bool enabled);

private:
    /**
     * @brief Append a page to the bar.
     * @param[in] label Page label.
     * @return The created page.
     */
    wxRibbonPage* AppendRibbonPage(const wxString& label);

    /**
     * @brief Append a panel holding one button bar.
     * @param[in] page Owning page.
     * @param[in] label Group label shown below the panel.
     * @return The button bar of the panel.
     */
    wxRibbonButtonBar* AppendButtonGroup(wxRibbonPage* page, const wxString& label);

    /**
     * @brief Add a button drawn with a large icon and a label below it.
     * @param[in] bar Owning button bar.
     * @param[in] id Command identifier.
     * @param[in] label Button label.
     * @param[in] art Art provider identifier of the icon.
     * @param[in] help Tooltip text.
     * @param[in] enabled Whether the button accepts input.
     */
    static void AddLargeButton(wxRibbonButtonBar* bar, int id, const wxString& label, const wxString& art,
                               const wxString& help, bool enabled);

    /**
     * @brief Add a button drawn with a small icon left of the label.
     * @param[in] bar Owning button bar.
     * @param[in] id Command identifier.
     * @param[in] label Button label.
     * @param[in] art Art provider identifier of the icon.
     * @param[in] help Tooltip text.
     * @param[in] enabled Whether the button accepts input.
     */
    static void AddSmallButton(wxRibbonButtonBar* bar, int id, const wxString& label, const wxString& art,
                               const wxString& help, bool enabled);

    /**
     * @brief Append the Output group with the archive path and project type.
     * @param[in] page Owning page.
     */
    void AppendOutputGroup(wxRibbonPage* page);

    /**
     * @brief Build the Home page with the workflow groups.
     */
    void CreateHomePage();

    /**
     * @brief Build the Advanced page with the reserved groups.
     */
    void CreateAdvancedPage();

    wxTextCtrl* output_path_ = nullptr;

    /**
     * @brief The `Project Type` box of the Output group.
     *
     * The box lists the project types in the order of ProjectTypeAt(), so the
     * selection is the index of the type it shows.
     */
    wxComboBox* project_type_ = nullptr;

    /**
     * @brief The button bar of the Build group.
     *
     * The bar is kept because the "Build and Run" button is enabled and
     * disabled while the session runs.
     */
    wxRibbonButtonBar* build_bar_ = nullptr;
};

#endif // APPBOX_PACKER_WIDGET_RIBBON_BAR_HPP
