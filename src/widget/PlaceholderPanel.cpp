#include "PlaceholderPanel.hpp"
#include <wx/artprov.h>
#include <wx/dcbuffer.h>

namespace
{

/** Horizontal margin of the header and of the empty state frame. */
constexpr int kMargin = 20;

/** Vertical position of the title. */
constexpr int kTitleTop = 16;

/** Vertical position of the summary line. */
constexpr int kSummaryTop = 42;

/** Vertical position of the separator below the header. */
constexpr int kSeparatorTop = 68;

/** Vertical position of the empty state frame. */
constexpr int kFrameTop = 88;

/** Edge length of the empty state icon. */
constexpr int kIconSize = 48;

/** Workspace background. */
const wxColour kBackgroundColour(0xFF, 0xFF, 0xFF);

/** Background of the framed empty state area. */
const wxColour kFrameColour(0xFA, 0xFA, 0xFA);

/** Border of the framed empty state area. */
const wxColour kFrameBorderColour(0xC8, 0xC8, 0xC8);

/** Colour of the header separator. */
const wxColour kSeparatorColour(0xE4, 0xE6, 0xE9);

/** Colour of the title. */
const wxColour kTitleColour(0x1F, 0x1F, 0x1F);

/** Colour of the summary line and of the empty state hint. */
const wxColour kMutedTextColour(0x5A, 0x5A, 0x5A);

} // namespace

PlaceholderPanel::PlaceholderPanel(wxWindow* parent, const wxString& module_name,
                                   const wxString& description)
    : wxPanel(parent, wxID_ANY),
      module_name_(module_name),
      description_(description)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &PlaceholderPanel::OnPaint, this);
}

void PlaceholderPanel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kBackgroundColour));
    dc.Clear();

    const auto size = GetClientSize();
    const auto width = size.GetWidth();

    dc.SetFont(wxFont(wxFontInfo(12).Bold()));
    dc.SetTextForeground(kTitleColour);
    dc.DrawText(module_name_, kMargin, kTitleTop);

    dc.SetFont(wxFont(wxFontInfo(9)));
    dc.SetTextForeground(kMutedTextColour);
    dc.DrawText(description_, kMargin, kSummaryTop);

    dc.SetPen(wxPen(kSeparatorColour));
    dc.DrawLine(kMargin, kSeparatorTop, width - kMargin, kSeparatorTop);

    const int frame_height = size.GetHeight() - kFrameTop - kMargin;
    if (frame_height <= 0 || width <= 2 * kMargin)
    {
        return;
    }

    const int frame_width = width - 2 * kMargin;
    dc.SetBrush(wxBrush(kFrameColour));
    dc.SetPen(wxPen(kFrameBorderColour));
    dc.DrawRectangle(kMargin, kFrameTop, frame_width, frame_height);

    const auto icon = wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_OTHER,
                                               wxSize(kIconSize, kIconSize));
    const int content_top = kFrameTop + (frame_height - (kIconSize + 52)) / 2;

    dc.DrawBitmap(icon, kMargin + (frame_width - kIconSize) / 2, content_top, true);

    dc.SetFont(wxFont(wxFontInfo(10).Bold()));
    dc.SetTextForeground(kTitleColour);
    const wxString headline = "Not implemented yet";
    dc.DrawText(headline, kMargin + (frame_width - dc.GetTextExtent(headline).GetWidth()) / 2,
                content_top + kIconSize + 14);

    dc.SetFont(wxFont(wxFontInfo(9)));
    dc.SetTextForeground(kMutedTextColour);
    const wxString hint = "This isolation domain is reserved for a future release.";
    dc.DrawText(hint, kMargin + (frame_width - dc.GetTextExtent(hint).GetWidth()) / 2,
                content_top + kIconSize + 36);
}
