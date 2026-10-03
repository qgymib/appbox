#include "TracerListModel.hpp"
#include <utility>

namespace
{

/** Colour of the rows the last run did not use. */
const wxColour kUnusedTextColour(0x5A, 0x5A, 0x5A);

} // namespace

TracerListModel::TracerListModel() : wxDataViewVirtualListModel(0)
{
}

void TracerListModel::SetEntries(std::vector<appbox::TracerEntry> entries)
{
    entries_ = std::move(entries);
    Reset(static_cast<unsigned int>(entries_.size()));
}

void TracerListModel::GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const
{
    variant = (col == FunctionColumn && row < entries_.size()) ? wxVariant(wxString(entries_[row].name))
                                                               : wxVariant(wxString());
}

bool TracerListModel::SetValueByRow(const wxVariant&, unsigned int, unsigned int)
{
    /* The list is read only: a run reports the functions, the user does not edit them. */
    return false;
}

bool TracerListModel::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const
{
    if (col != FunctionColumn || row >= entries_.size() || entries_[row].used)
    {
        /* The default attribute draws in the default (black) colour, which the used rows keep. */
        return false;
    }

    attr.SetColour(kUnusedTextColour);
    return true;
}
