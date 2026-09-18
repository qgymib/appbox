#include "SideNav.hpp"
#include <wx/artprov.h>
#include <wx/dcbuffer.h>

wxDEFINE_EVENT(APPBOX_SIDE_NAV, wxCommandEvent);

namespace
{

/** Width of the control. */
constexpr int kNavWidth = 132;

/** Height of the group caption at the top. */
constexpr int kCaptionHeight = 28;

/** Height of one navigation item. */
constexpr int kItemHeight = 34;

/** Width of the accent bar of the selected item. */
constexpr int kAccentWidth = 3;

/** Left margin of the item icons. */
constexpr int kIconLeft = 12;

/** Left margin of the item labels. */
constexpr int kLabelLeft = 36;

/** Edge length of the item icons. */
constexpr int kIconSize = 16;

/** Background of the navigation column. */
const wxColour kBackgroundColour(0xF2, 0xF3, 0xF5);

/** Background of the selected item. */
const wxColour kSelectionColour(0xDC, 0xEB, 0xFB);

/** Background of the hovered item. */
const wxColour kHoverColour(0xE8, 0xEE, 0xF6);

/** Accent bar of the selected item. */
const wxColour kAccentColour(0x16, 0x68, 0xC1);

/** Separator between the navigation column and the workspace. */
const wxColour kBorderColour(0xC8, 0xC8, 0xC8);

/** Label colour of a regular item. */
const wxColour kTextColour(0x1F, 0x1F, 0x1F);

/** Label colour of the selected item. */
const wxColour kSelectionTextColour(0x0B, 0x4C, 0x8C);

/** Colour of the group caption. */
const wxColour kCaptionColour(0x5A, 0x5A, 0x5A);

} // namespace

SideNav::SideNav(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxSize(kNavWidth, -1), wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(kNavWidth, -1));

    Bind(wxEVT_PAINT, &SideNav::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &SideNav::OnLeftDown, this);
    Bind(wxEVT_MOTION, &SideNav::OnMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, &SideNav::OnLeaveWindow, this);
}

void SideNav::AddItem(const wxString& label, const wxString& art)
{
    Item item;
    item.label = label;
    item.icon = wxArtProvider::GetBitmap(art, wxART_OTHER, wxSize(kIconSize, kIconSize));
    items_.push_back(std::move(item));

    Refresh();
}

int SideNav::HitTest(const wxPoint& position) const
{
    if (position.y < kCaptionHeight)
    {
        return wxNOT_FOUND;
    }

    const auto index = (position.y - kCaptionHeight) / kItemHeight;
    if (index < 0 || static_cast<std::size_t>(index) >= items_.size())
    {
        return wxNOT_FOUND;
    }
    return index;
}

void SideNav::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kBackgroundColour));
    dc.Clear();

    const auto size = GetClientSize();

    dc.SetFont(wxFont(wxFontInfo(8)));
    dc.SetTextForeground(kCaptionColour);
    dc.DrawText("Default", kIconLeft, 8);

    dc.SetPen(wxPen(kBorderColour));
    dc.DrawLine(size.GetWidth() - 1, 0, size.GetWidth() - 1, size.GetHeight());

    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const auto top = kCaptionHeight + static_cast<int>(i) * kItemHeight;
        const bool selected = i == selection_;
        const bool hovered = static_cast<int>(i) == hovered_;

        if (selected)
        {
            dc.SetBrush(wxBrush(kSelectionColour));
        }
        else if (hovered)
        {
            dc.SetBrush(wxBrush(kHoverColour));
        }
        else
        {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
        }
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, top, size.GetWidth() - 1, kItemHeight);

        if (selected)
        {
            dc.SetBrush(wxBrush(kAccentColour));
            dc.DrawRectangle(0, top, kAccentWidth, kItemHeight);
        }

        dc.DrawBitmap(items_[i].icon, kIconLeft, top + (kItemHeight - kIconSize) / 2, true);

        dc.SetFont(wxFont(wxFontInfo(9)));
        dc.SetTextForeground(selected ? kSelectionTextColour : kTextColour);
        dc.DrawText(items_[i].label, kLabelLeft, top + (kItemHeight - dc.GetCharHeight()) / 2);
    }
}

void SideNav::OnLeftDown(wxMouseEvent& event)
{
    Select(HitTest(event.GetPosition()));
    event.Skip();
}

void SideNav::OnMotion(wxMouseEvent& event)
{
    const auto hovered = HitTest(event.GetPosition());
    if (hovered != hovered_)
    {
        hovered_ = hovered;
        Refresh();
    }
    event.Skip();
}

void SideNav::OnLeaveWindow(wxMouseEvent& event)
{
    if (hovered_ != wxNOT_FOUND)
    {
        hovered_ = wxNOT_FOUND;
        Refresh();
    }
    event.Skip();
}

void SideNav::Select(int index)
{
    if (index == wxNOT_FOUND || static_cast<std::size_t>(index) >= items_.size())
    {
        return;
    }

    if (static_cast<std::size_t>(index) == selection_)
    {
        return;
    }

    selection_ = static_cast<std::size_t>(index);
    Refresh();

    wxCommandEvent changed(APPBOX_SIDE_NAV, GetId());
    changed.SetEventObject(this);
    changed.SetInt(index);
    GetParent()->GetEventHandler()->ProcessEvent(changed);
}
