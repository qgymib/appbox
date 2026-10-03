#ifndef APPBOX_PACKER_WIDGET_TRACER_LIST_MODEL_HPP
#define APPBOX_PACKER_WIDGET_TRACER_LIST_MODEL_HPP

/*
 * wx/wx.h comes first on purpose: including the wxWidgets headers in another
 * order makes MSVC report the deprecated CRT calls of wx/wxcrt.h (C4996),
 * which the project builds as an error.
 */
#include <wx/wx.h>
#include <wx/dataview.h>
#include "core/TracerModel.hpp"
#include <vector>

/**
 * @brief Flat list model of the tracer workspace.
 *
 * The model presents the rows of the current view as a single column list. A
 * row whose function was used by the last run keeps the default text colour;
 * a row which the run did not report is drawn in the muted colour, so the
 * functions the run used stand out in front of the ones it did not.
 */
class TracerListModel : public wxDataViewVirtualListModel
{
public:
    /**
     * @brief One column of the model: the `module!function` name of the row.
     */
    enum Column
    {
        FunctionColumn = 0 ///< The only column of the list.
    };

    /** Create an empty model. */
    TracerListModel();

    /**
     * @brief Replace the rows of the model.
     * @param[in] entries Rows to show, in the order they are shown.
     */
    void SetEntries(std::vector<appbox::TracerEntry> entries);

    void GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;
    bool SetValueByRow(const wxVariant& variant, unsigned int row, unsigned int col) override;
    bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const override;

private:
    std::vector<appbox::TracerEntry> entries_; ///< Rows which are shown.
};

#endif // APPBOX_PACKER_WIDGET_TRACER_LIST_MODEL_HPP
