#ifndef APPBOX_PACKER_WIDGET_RIBBON_BAR_HPP
#define APPBOX_PACKER_WIDGET_RIBBON_BAR_HPP

#include <wx/wx.h>
#include <wx/ribbon/bar.h>
#include <wx/ribbon/buttonbar.h>
#include <wx/ribbon/page.h>
#include <wx/ribbon/panel.h>

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
    static void AddLargeButton(wxRibbonButtonBar* bar, int id, const wxString& label,
                               const wxString& art, const wxString& help, bool enabled);

    /**
     * @brief Add a button drawn with a small icon left of the label.
     * @param[in] bar Owning button bar.
     * @param[in] id Command identifier.
     * @param[in] label Button label.
     * @param[in] art Art provider identifier of the icon.
     * @param[in] help Tooltip text.
     * @param[in] enabled Whether the button accepts input.
     */
    static void AddSmallButton(wxRibbonButtonBar* bar, int id, const wxString& label,
                               const wxString& art, const wxString& help, bool enabled);

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
};

#endif // APPBOX_PACKER_WIDGET_RIBBON_BAR_HPP
