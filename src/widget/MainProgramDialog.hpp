#ifndef APPBOX_PACKER_WIDGET_MAIN_PROGRAM_DIALOG_HPP
#define APPBOX_PACKER_WIDGET_MAIN_PROGRAM_DIALOG_HPP

#include "StartupTreeModel.hpp"
#include "core/PackModel.hpp"
#include <wx/dataview.h>
#include <wx/wx.h>

/**
 * @brief Dialog to browse the imported folders and pick the main program.
 *
 * The dialog shows the startup file tree as a tree table with the columns
 * Name, Type and Startup. The Startup column carries a checkbox which marks
 * the executable started by the packaged application; checking an executable
 * unchecks the previous one, so exactly one startup file can be chosen.
 *
 * Confirming is only possible while an executable is checked. The main
 * program of the model is checked and shown when the dialog opens.
 */
class MainProgramDialog : public wxDialog
{
public:
    /**
     * @brief Create the main program browser.
     * @param[in] parent Parent window.
     * @param[in] model The pack model holding the imports.
     */
    MainProgramDialog(wxWindow* parent, const appbox::PackModel& model);

    /**
     * @brief Get the selection made by the user.
     *
     * Only valid after ShowModal() returned wxID_OK.
     *
     * @return The selected main program.
     */
    const appbox::MainProgram& Selection() const;

private:
    /**
     * @brief Show the current startup file and expand the tree to it.
     * @param[in] choice The startup file to reveal.
     */
    void RevealSelection(const appbox::MainProgram& choice);

    /**
     * @brief Enable or disable the OK button for the current state.
     */
    void UpdateOkButton();

    /**
     * @brief Close the dialog with the checked executable.
     */
    void Accept();

    /**
     * @brief Check an executable activated by double click or Enter.
     * @param[in] event Tree item activation event.
     */
    void OnItemActivated(wxDataViewEvent& event);

    /**
     * @brief Update the OK button after the checked row changed.
     * @param[in] event Data view item value change event.
     */
    void OnValueChanged(wxDataViewEvent& event);

    /**
     * @brief Validate the selection before closing with OK.
     * @param[in] event Command event.
     */
    void OnOk(wxCommandEvent& event);

    StartupTreeModel*  tree_model_ = nullptr;
    wxDataViewCtrl*    tree_ = nullptr;
    wxButton*          ok_button_ = nullptr;
    appbox::MainProgram selection_;
};

#endif // APPBOX_PACKER_WIDGET_MAIN_PROGRAM_DIALOG_HPP
