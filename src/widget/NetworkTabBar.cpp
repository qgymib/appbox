#include "NetworkTabBar.hpp"
#include <wx/dcbuffer.h>
#include <algorithm>
#include <utility>

wxDEFINE_EVENT(APPBOX_NETWORK_TAB, wxCommandEvent);

namespace
{

/** Height of the strip. */
constexpr int kTabBarHeight = 28;

/** Horizontal padding of the label of a tab. */
constexpr int kTabPadding = 24;

/** Smallest width of a tab. */
constexpr int kMinTabWidth = 90;

/** Height of the accent bar of the selected tab. */
constexpr int kAccentHeight = 2;

/** Background of the strip. */
const wxColour kBarBackgroundColour(0xF2, 0xF3, 0xF5);

/** Separator between the strip and the page below it. */
const wxColour kBorderColour(0xC8, 0xC8, 0xC8);

/** Background of the selected tab. */
const wxColour kSelectedBackgroundColour(0xFF, 0xFF, 0xFF);

/** Background of the hovered tab. */
const wxColour kHoverBackgroundColour(0xE8, 0xEE, 0xF6);

/** Accent bar of the selected tab. */
const wxColour kAccentColour(0x16, 0x68, 0xC1);

/** Label colour of an unselected tab. */
const wxColour kTextColour(0x5A, 0x5A, 0x5A);

/** Label colour of the selected tab. */
const wxColour kSelectedTextColour(0x1F, 0x1F, 0x1F);

/**
 * @brief Get the font of the labels of the strip.
 *
 * The width of a tab is measured with this font for every tab, so switching
 * the selection never moves a tab.
 *
 * @return The font of the labels.
 */
wxFont TabFont()
{
    return wxFont(wxFontInfo(9));
}

} // namespace

NetworkTabBar::NetworkTabBar(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxSize(-1, kTabBarHeight), wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, kTabBarHeight));

    Bind(wxEVT_PAINT, &NetworkTabBar::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &NetworkTabBar::OnLeftDown, this);
    Bind(wxEVT_MOTION, &NetworkTabBar::OnMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, &NetworkTabBar::OnLeaveWindow, this);
}

void NetworkTabBar::AddTab(const wxString& label)
{
    Tab tab;
    tab.label = label;

    /*
     * The width is measured once, with the font of an unselected tab: the
     * painting and the hit test both use it, so a selection change never
     * moves a tab and both agree on where a tab is.
     */
    const wxFont font = TabFont();
    int          text_width = 0;
    int          text_height = 0;
    GetTextExtent(label, &text_width, &text_height, nullptr, nullptr, &font);
    tab.width = std::max(kMinTabWidth, text_width + 2 * kTabPadding);

    tabs_.push_back(std::move(tab));

    Refresh();
}

int NetworkTabBar::GetSelection() const
{
    return tabs_.empty() ? wxNOT_FOUND : static_cast<int>(selection_);
}

void NetworkTabBar::SetSelection(int index)
{
    if (index == wxNOT_FOUND || static_cast<std::size_t>(index) >= tabs_.size() ||
        static_cast<std::size_t>(index) == selection_)
    {
        return;
    }

    selection_ = static_cast<std::size_t>(index);
    Refresh();
}

std::vector<wxRect> NetworkTabBar::TabRects() const
{
    std::vector<wxRect> rects;
    rects.reserve(tabs_.size());

    int left = 0;
    for (const auto& tab : tabs_)
    {
        rects.emplace_back(left, 0, tab.width, kTabBarHeight);
        left += tab.width;
    }
    return rects;
}

int NetworkTabBar::HitTest(const wxPoint& position) const
{
    const auto rects = TabRects();
    for (std::size_t index = 0; index < rects.size(); ++index)
    {
        if (rects[index].Contains(position))
        {
            return static_cast<int>(index);
        }
    }
    return wxNOT_FOUND;
}

void NetworkTabBar::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kBarBackgroundColour));
    dc.Clear();

    const auto size = GetClientSize();
    const auto rects = TabRects();

    const int bottom = std::min(size.GetHeight(), kTabBarHeight) - 1;
    dc.SetPen(wxPen(kBorderColour));
    dc.DrawLine(0, bottom, size.GetWidth(), bottom);

    const wxFont regular = TabFont();
    const wxFont bold = TabFont().Bold();

    for (std::size_t index = 0; index < rects.size(); ++index)
    {
        const auto& rect = rects[index];
        const bool  selected = index == selection_;
        const bool  hovered = static_cast<int>(index) == hovered_;

        if (selected)
        {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(kSelectedBackgroundColour));
            dc.DrawRectangle(rect.x, rect.y, rect.width, rect.height - 1);

            dc.SetBrush(wxBrush(kAccentColour));
            dc.DrawRectangle(rect.x, rect.y, rect.width, kAccentHeight);
        }
        else if (hovered)
        {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(kHoverBackgroundColour));
            dc.DrawRectangle(rect.x, rect.y, rect.width, rect.height - 1);
        }

        dc.SetFont(selected ? bold : regular);
        dc.SetTextForeground(selected ? kSelectedTextColour : kTextColour);

        const auto extent = dc.GetTextExtent(tabs_[index].label);
        dc.DrawText(tabs_[index].label, rect.x + (rect.width - extent.GetWidth()) / 2,
                    rect.y + (rect.height - 1 - extent.GetHeight()) / 2);
    }
}

void NetworkTabBar::OnLeftDown(wxMouseEvent& event)
{
    Select(HitTest(event.GetPosition()));
    event.Skip();
}

void NetworkTabBar::OnMotion(wxMouseEvent& event)
{
    const auto hovered = HitTest(event.GetPosition());
    if (hovered != hovered_)
    {
        hovered_ = hovered;
        Refresh();
    }
    event.Skip();
}

void NetworkTabBar::OnLeaveWindow(wxMouseEvent& event)
{
    if (hovered_ != wxNOT_FOUND)
    {
        hovered_ = wxNOT_FOUND;
        Refresh();
    }
    event.Skip();
}

void NetworkTabBar::Select(int index)
{
    if (index == wxNOT_FOUND || static_cast<std::size_t>(index) >= tabs_.size() ||
        static_cast<std::size_t>(index) == selection_)
    {
        return;
    }

    selection_ = static_cast<std::size_t>(index);
    Refresh();

    wxCommandEvent changed(APPBOX_NETWORK_TAB, GetId());
    changed.SetEventObject(this);
    changed.SetInt(index);
    GetParent()->GetEventHandler()->ProcessEvent(changed);
}
