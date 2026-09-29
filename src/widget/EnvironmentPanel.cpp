#include "EnvironmentPanel.hpp"
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <memory>
#include <utility>

namespace
{

/** Model column of the name of a variable. */
constexpr unsigned int kNameColumn = 0;

/** Model column of the value of a variable. */
constexpr unsigned int kValueColumn = 1;

/** Model column of the isolation mode of a variable. */
constexpr unsigned int kIsolationColumn = 2;

/** Model column of the merge mode of a variable. */
constexpr unsigned int kMergeColumn = 3;

/** Model column of the merge string of a variable. */
constexpr unsigned int kMergeStringColumn = 4;

/** Width of the name column. */
constexpr int kNameWidth = 180;

/** Width of the value column. */
constexpr int kValueWidth = 260;

/** Width of a mode column. */
constexpr int kModeWidth = 110;

/** Width of the merge string column. */
constexpr int kMergeStringWidth = 120;

/** Background of the toolbar row above the table. */
const wxColour kToolBarBackground(0xF2, 0xF3, 0xF5);

/** Tooltip of the name column. */
const char* const kNameTooltip =
    "Name of the environment variable inside the sandbox.\n"
    "The environment of a process ignores the case, so 'Path' names the same variable as 'PATH'.\n"
    "A name is listed once and must not carry an equals sign.";

/** Tooltip of the value column. */
const char* const kValueTooltip =
    "Value of the variable.\n"
    "What the application sees depends on the isolation mode and on the merge mode of the row.";

/** Tooltip of the merge string column. */
const char* const kMergeStringTooltip =
    "Text which joins the value of the row and the value of the host.\n"
    "It is used by the merge modes 'Prepend' and 'Append' and is ignored by 'Replace' and 'Host'.\n"
    "A search path uses ';', which the workspace fills in for the search path variable.";

/** Hint appended to the tooltip of the mode of the search path variable. */
const char* const kPathVariableHint =
    "\nThe search path variable is filled in with 'Prepend' and ';' while its name is entered; the two values can be "
    "changed afterwards.";

/** Lead of the tooltip of the header of the `IsolationMode` column. */
const char* const kIsolationColumnLead = "Isolation mode of the variable, which decides what the application sees.\n"
                                         "The value of the host is only visible while the mode is 'Write Copy'.";

/** Lead of the tooltip of the header of the `MergeMode` column. */
const char* const kMergeColumnLead =
    "Merge mode of the variable, which decides how the stored value and the value of the host are joined while the "
    "isolation mode is 'Write Copy'.\n"
    "The mode is ignored while the isolation mode is 'Full'.";

/**
 * @brief Describe the modes the `IsolationMode` column offers.
 * @return The description of the column.
 */
wxString IsolationColumnTooltip()
{
    wxString text = kIsolationColumnLead;
    for (const auto isolation : { appbox::EnvironmentIsolation::Full, appbox::EnvironmentIsolation::WriteCopy })
    {
        text += "\n\n";
        text += wxString(appbox::EnvironmentIsolationDescription(isolation));
    }
    return text;
}

/**
 * @brief Describe the modes the `MergeMode` column offers.
 * @return The description of the column.
 */
wxString MergeColumnTooltip()
{
    wxString text = kMergeColumnLead;
    for (const auto merge : { appbox::EnvironmentMergeMode::Replace, appbox::EnvironmentMergeMode::Host,
                              appbox::EnvironmentMergeMode::Prepend, appbox::EnvironmentMergeMode::Append })
    {
        text += "\n\n";
        text += wxString(appbox::EnvironmentMergeModeDescription(merge));
    }
    return text;
}

/**
 * @brief Read the isolation mode a mode cell shows.
 *
 * The position of a display name in the list of the modes is the value of the
 * mode it names, so the text of the cell is mapped back by its position.
 *
 * @param[in] name Text of the cell.
 * @param[out] out The mode of the cell, untouched when the text names none.
 * @return true when the text names an isolation mode.
 */
bool IsolationOfName(const wxString& name, appbox::EnvironmentIsolation& out)
{
    const auto& names = appbox::EnvironmentIsolationNames();
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (wxString(names[index]) == name)
        {
            out = static_cast<appbox::EnvironmentIsolation>(index);
            return true;
        }
    }
    return false;
}

/**
 * @brief Read the merge mode a mode cell shows.
 * @param[in] name Text of the cell.
 * @param[out] out The mode of the cell, untouched when the text names none.
 * @return true when the text names a merge mode.
 */
bool MergeModeOfName(const wxString& name, appbox::EnvironmentMergeMode& out)
{
    const auto& names = appbox::EnvironmentMergeModeNames();
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (wxString(names[index]) == name)
        {
            out = static_cast<appbox::EnvironmentMergeMode>(index);
            return true;
        }
    }
    return false;
}

} // namespace

EnvironmentPanel::EnvironmentPanel(wxWindow* parent, appbox::EnvironmentModel& model)
    : wxPanel(parent, wxID_ANY), model_(model)
{
    auto* toolbar = CreateToolBarRow(this);
    CreateList(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(toolbar, 0, wxEXPAND);
    sizer->Add(list_, 1, wxEXPAND);
    SetSizer(sizer);

    RefreshList();
}

wxWindow* EnvironmentPanel::CreateToolBarRow(wxWindow* parent)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(kToolBarBackground);

    add_ = new wxButton(row, wxID_ANY, "Add", wxDefaultPosition, wxSize(-1, 26));
    add_->SetToolTip("Add an environment variable to the view");
    add_->Bind(wxEVT_BUTTON, &EnvironmentPanel::OnAdd, this);

    remove_ = new wxButton(row, wxID_ANY, "Remove", wxDefaultPosition, wxSize(-1, 26));
    remove_->SetToolTip("Remove the selected environment variable");
    remove_->Bind(wxEVT_BUTTON, &EnvironmentPanel::OnRemove, this);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(add_, 0, wxALL, 4);
    sizer->Add(remove_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);

    row->SetSizer(sizer);
    return row;
}

void EnvironmentPanel::CreateList(wxWindow* parent)
{
    list_ = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);

    /*
     * The three text columns are edited inside the cell: the value of a row is
     * entered the way the table shows it, and the model is updated once the
     * cell was committed.
     */
    list_->AppendTextColumn("Name", wxDATAVIEW_CELL_EDITABLE, kNameWidth);
    list_->AppendTextColumn("Value", wxDATAVIEW_CELL_EDITABLE, kValueWidth);

    /*
     * The two mode columns use a choice renderer: a mode is picked from a
     * dropdown instead of being typed, so the table never holds a mode the
     * model does not know. The renderer is added through AppendColumn()
     * because it has to claim the model column of the table explicitly.
     */
    wxArrayString isolation_choices;
    for (const auto& name : appbox::EnvironmentIsolationNames())
    {
        isolation_choices.Add(wxString(name));
    }
    list_->AppendColumn(new wxDataViewColumn("IsolationMode",
                                             new wxDataViewChoiceRenderer(isolation_choices, wxDATAVIEW_CELL_EDITABLE),
                                             kIsolationColumn, kModeWidth));

    wxArrayString merge_choices;
    for (const auto& name : appbox::EnvironmentMergeModeNames())
    {
        merge_choices.Add(wxString(name));
    }
    list_->AppendColumn(new wxDataViewColumn(
        "MergeMode", new wxDataViewChoiceRenderer(merge_choices, wxDATAVIEW_CELL_EDITABLE), kMergeColumn, kModeWidth));

    list_->AppendTextColumn("MergeString", wxDATAVIEW_CELL_EDITABLE, kMergeStringWidth);

    list_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        UpdateToolBarState();
        event.Skip();
    });
    list_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &EnvironmentPanel::OnValueChanged, this);

    /*
     * The tooltip of the table describes the cell and the column of the header
     * below the cursor, see DataViewTooltip.
     */
    tooltip_ = std::make_unique<DataViewTooltip>(
        *list_, [this](int row, int column) { return TooltipForCell(row, column); },
        [this](int column) { return TooltipForHeader(column); });
}

void EnvironmentPanel::RefreshModel()
{
    /*
     * The draft is dropped as well: it belongs to the model which was replaced,
     * so the table shows the variables of the new model only.
     */
    rows_.clear();
    RefreshList();
}

void EnvironmentPanel::RefreshList()
{
    updating_ = true;
    list_->DeleteAllItems();

    rows_.clear();
    for (const auto& entry : model_.Entries())
    {
        RowInfo row;
        row.name = entry.name;
        row.value = entry.value;
        row.isolation = entry.isolation;
        row.merge = entry.merge;
        row.merge_string = entry.merge_string;
        rows_.push_back(std::move(row));
    }

    for (const auto& row : rows_)
    {
        AppendRow(row);
    }

    updating_ = false;
    UpdateToolBarState();
}

void EnvironmentPanel::AppendRow(const RowInfo& row)
{
    wxVector<wxVariant> values;
    values.push_back(wxVariant(wxString(row.name)));
    values.push_back(wxVariant(wxString(row.value)));
    values.push_back(wxVariant(wxString(appbox::EnvironmentIsolationName(row.isolation))));
    values.push_back(wxVariant(wxString(appbox::EnvironmentMergeModeName(row.merge))));
    values.push_back(wxVariant(wxString(row.merge_string)));
    list_->AppendItem(values);
}

void EnvironmentPanel::UpdateToolBarState()
{
    if (remove_ == nullptr || list_ == nullptr)
    {
        return;
    }

    remove_->Enable(list_->GetSelectedRow() >= 0);
}

void EnvironmentPanel::BeginEditRow(int row)
{
    list_->CallAfter([this, row]() {
        if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        {
            return;
        }

        list_->SelectRow(static_cast<unsigned int>(row));
        list_->EditItem(list_->RowToItem(row), list_->GetColumn(kNameColumn));
        UpdateToolBarState();
    });
}

void EnvironmentPanel::ShowStoredModes(int row, const appbox::EnvironmentEntry& entry)
{
    const wxString isolation = wxString(appbox::EnvironmentIsolationName(entry.isolation));
    const wxString merge = wxString(appbox::EnvironmentMergeModeName(entry.merge));
    const wxString merge_string = wxString(entry.merge_string);

    /*
     * The event which stored the entry is still being dispatched, so the cells
     * are filled once it was handled; the search path rule changes the mode and
     * the string of an entry, and the table has to show what was stored.
     */
    list_->CallAfter([this, row, isolation, merge, merge_string]() {
        if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        {
            return;
        }

        updating_ = true;
        list_->SetTextValue(isolation, static_cast<unsigned int>(row), kIsolationColumn);
        list_->SetTextValue(merge, static_cast<unsigned int>(row), kMergeColumn);
        list_->SetTextValue(merge_string, static_cast<unsigned int>(row), kMergeStringColumn);
        updating_ = false;
    });
}

void EnvironmentPanel::RevertCell(int row, unsigned int column, const wxString& value)
{
    updating_ = true;
    list_->SetTextValue(value, static_cast<unsigned int>(row), column);
    updating_ = false;
}

void EnvironmentPanel::ReportError(const std::string& error)
{
    wxMessageBox(wxString::FromUTF8(error), "Environment Variable", wxOK | wxICON_ERROR, this);
}

appbox::EnvironmentEntry EnvironmentPanel::EntryOfRow(int row) const
{
    const auto index = static_cast<unsigned int>(row);

    appbox::EnvironmentEntry entry;
    entry.name = list_->GetTextValue(index, kNameColumn).ToStdWstring();
    entry.value = list_->GetTextValue(index, kValueColumn).ToStdWstring();

    appbox::EnvironmentIsolation isolation = appbox::EnvironmentIsolation::WriteCopy;
    if (IsolationOfName(list_->GetTextValue(index, kIsolationColumn), isolation))
    {
        entry.isolation = isolation;
    }

    appbox::EnvironmentMergeMode merge = appbox::environment_isolation::kDefaultMergeMode;
    if (MergeModeOfName(list_->GetTextValue(index, kMergeColumn), merge))
    {
        entry.merge = merge;
    }

    entry.merge_string = list_->GetTextValue(index, kMergeStringColumn).ToStdWstring();
    return entry;
}

wxString EnvironmentPanel::TooltipForCell(int row, int column) const
{
    if (column == static_cast<int>(kNameColumn))
    {
        return kNameTooltip;
    }
    if (column == static_cast<int>(kValueColumn))
    {
        return kValueTooltip;
    }
    if (column == static_cast<int>(kMergeStringColumn))
    {
        return kMergeStringTooltip;
    }

    const bool is_isolation = column == static_cast<int>(kIsolationColumn);
    const bool is_merge = column == static_cast<int>(kMergeColumn);
    if ((!is_isolation && !is_merge) || row < 0 || static_cast<std::size_t>(row) >= rows_.size())
    {
        return {};
    }

    const auto& info = rows_[static_cast<std::size_t>(row)];

    wxString text = is_isolation ? wxString(appbox::EnvironmentIsolationDescription(info.isolation))
                                 : wxString(appbox::EnvironmentMergeModeDescription(info.merge));

    if (appbox::environment_isolation::IsPathVariableName(info.name))
    {
        text += kPathVariableHint;
    }
    return text;
}

wxString EnvironmentPanel::TooltipForHeader(int column) const
{
    if (column == static_cast<int>(kNameColumn))
    {
        return kNameTooltip;
    }
    if (column == static_cast<int>(kValueColumn))
    {
        return kValueTooltip;
    }
    if (column == static_cast<int>(kIsolationColumn))
    {
        return IsolationColumnTooltip();
    }
    if (column == static_cast<int>(kMergeColumn))
    {
        return MergeColumnTooltip();
    }
    if (column == static_cast<int>(kMergeStringColumn))
    {
        return kMergeStringTooltip;
    }

    return {};
}

void EnvironmentPanel::OnAdd(wxCommandEvent&)
{
    /*
     * The table keeps at most one row which is still being filled in, so a
     * second `Add` selects that row instead of appending another one. Its
     * editor is not opened again: the control keeps the editor it created
     * while the row is being filled in, and a second one would stay behind as
     * an unused control of the same cell.
     */
    if (!rows_.empty() && rows_.back().pending)
    {
        list_->SelectRow(static_cast<unsigned int>(rows_.size()) - 1);
        UpdateToolBarState();
        return;
    }

    RowInfo draft;
    draft.pending = true;
    rows_.push_back(std::move(draft));

    updating_ = true;
    AppendRow(rows_.back());
    updating_ = false;

    const auto index = static_cast<int>(rows_.size()) - 1;
    list_->SelectRow(static_cast<unsigned int>(index));
    UpdateToolBarState();
    BeginEditRow(index);
}

void EnvironmentPanel::OnRemove(wxCommandEvent&)
{
    const auto row = list_->GetSelectedRow();
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
    {
        return;
    }

    const auto index = static_cast<std::size_t>(row);

    /*
     * The stored rows are the entries of the model in model order, so the
     * index of a stored row is the index of its entry as well. A draft row
     * exists in the table only and is dropped with the row itself.
     */
    if (!rows_[index].pending && !model_.RemoveEntry(index))
    {
        return;
    }

    /*
     * Only the removed row is dropped. Rebuilding the whole table instead
     * would drop the item an editor of another row is bound to, and a row
     * which the user did not touch keeps its state.
     */
    updating_ = true;
    list_->DeleteItem(static_cast<unsigned int>(row));
    updating_ = false;

    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(index));
    list_->UnselectAll();
    UpdateToolBarState();
}

void EnvironmentPanel::OnValueChanged(wxDataViewEvent& event)
{
    if (updating_)
    {
        return;
    }

    const auto row = list_->ItemToRow(event.GetItem());
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
    {
        return;
    }

    const auto index = static_cast<std::size_t>(row);
    auto       entry = EntryOfRow(row);

    std::string error;

    if (rows_[index].pending)
    {
        /*
         * A row which is still being filled in reaches the model once it
         * carries a name. A value the model refuses keeps the draft as the
         * user typed it, because a draft holds no stored value which could be
         * put back.
         */
        if (entry.name.empty())
        {
            rows_[index].name = entry.name;
            rows_[index].value = entry.value;
            rows_[index].isolation = entry.isolation;
            rows_[index].merge = entry.merge;
            rows_[index].merge_string = entry.merge_string;
            return;
        }

        /* The name of a new variable becomes the search path variable. */
        appbox::ApplyPathVariableDefaults(entry);

        if (!model_.AddEntry(entry, error))
        {
            ReportError(error);
            return;
        }

        rows_[index].name = entry.name;
        rows_[index].value = entry.value;
        rows_[index].isolation = entry.isolation;
        rows_[index].merge = entry.merge;
        rows_[index].merge_string = entry.merge_string;
        rows_[index].pending = false;
        ShowStoredModes(row, entry);
        return;
    }

    /*
     * The search path rule fires while the name becomes the search path
     * variable, so a mode and a string the user picked by hand afterwards are
     * kept.
     */
    if (appbox::environment_isolation::IsPathVariableName(entry.name) &&
        !appbox::environment_isolation::IsPathVariableName(rows_[index].name))
    {
        appbox::ApplyPathVariableDefaults(entry);
    }

    if (!model_.SetEntry(index, entry, error))
    {
        const auto column = static_cast<unsigned int>(event.GetColumn());
        switch (column)
        {
        case kNameColumn:
            RevertCell(row, column, wxString(rows_[index].name));
            break;
        case kValueColumn:
            RevertCell(row, column, wxString(rows_[index].value));
            break;
        case kIsolationColumn:
            RevertCell(row, column, wxString(appbox::EnvironmentIsolationName(rows_[index].isolation)));
            break;
        case kMergeColumn:
            RevertCell(row, column, wxString(appbox::EnvironmentMergeModeName(rows_[index].merge)));
            break;
        case kMergeStringColumn:
            RevertCell(row, column, wxString(rows_[index].merge_string));
            break;
        default:
            break;
        }

        ReportError(error);
        return;
    }

    rows_[index].name = entry.name;
    rows_[index].value = entry.value;
    rows_[index].isolation = entry.isolation;
    rows_[index].merge = entry.merge;
    rows_[index].merge_string = entry.merge_string;
    ShowStoredModes(row, entry);
}
