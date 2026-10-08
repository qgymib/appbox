#include "DebugPanel.hpp"
#include "TabBar.hpp"
#include "TracePanel.hpp"
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <cstddef>

namespace
{

/**
 * @brief Pages of the Debug workspace.
 *
 * The values are the positions of the pages inside the tab strip as well.
 */
enum class DebugPage
{
    Trace = 0 ///< Entry points a program really uses, reported by a run below the debugger.
};

/** Page the workspace opens on. */
constexpr DebugPage kStartPage = DebugPage::Trace;

} // namespace

DebugPanel::DebugPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    tab_bar_ = new TabBar(this, wxID_ANY);
    tab_bar_->AddTab("Trace");
    tab_bar_->SetSelection(static_cast<int>(kStartPage));

    /*
     * The book holds the page of every tab of the strip, so the two are kept
     * in step by adding a tab to both of them in the same order.
     */
    pages_ = new wxSimplebook(this, wxID_ANY);
    pages_->AddPage(CreateTracePage(pages_), "Trace");
    pages_->SetSelection(static_cast<std::size_t>(kStartPage));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tab_bar_, 0, wxEXPAND);
    sizer->Add(pages_, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(APPBOX_TAB, &DebugPanel::OnTabChanged, this);
}

wxWindow* DebugPanel::CreateTracePage(wxWindow* parent)
{
    return new TracePanel(parent);
}

void DebugPanel::OnTabChanged(wxCommandEvent& event)
{
    const auto index = event.GetInt();
    if (index >= 0 && static_cast<std::size_t>(index) < pages_->GetPageCount())
    {
        pages_->SetSelection(static_cast<std::size_t>(index));
    }
    event.Skip();
}
