#include "NetworkPanel.hpp"
#include "TabBar.hpp"
#include "PlaceholderPanel.hpp"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <utility>

namespace
{

/**
 * @brief Pages of the Network workspace.
 *
 * The values are the positions of the pages inside the tab strip as well.
 */
enum class NetworkPage
{
    Proxy = 0,         ///< Proxy configuration of the packaged application.
    Dns = 1,           ///< DNS redirections.
    IpRestrictions = 2 ///< Address restrictions, not implemented yet.
};

/** Page the workspace opens on. */
constexpr NetworkPage kStartPage = NetworkPage::Proxy;

/** Model column of the hostname. */
constexpr unsigned int kHostnameColumn = 0;

/** Model column of the redirect target. */
constexpr unsigned int kRedirectColumn = 1;

/** Width of the hostname column. */
constexpr int kHostnameWidth = 220;

/** Width of the redirect column. */
constexpr int kRedirectWidth = 300;

/** Background of the toolbar row above the DNS table. */
const wxColour kToolBarBackground(0xF2, 0xF3, 0xF5);

/** Border of the proxy form. */
constexpr int kFormBorder = 12;

/** Gap between two rows and between a label and its control. */
constexpr int kFieldGap = 8;

/** Width of the text fields of the proxy form. */
constexpr int kFieldWidth = 260;

/** Width of the port field of the proxy form. */
constexpr int kPortWidth = 80;

/** Number of digits the port field accepts. */
constexpr int kPortLength = 5;

/** Colour of the hint which describes the stored proxy configuration. */
const wxColour kMutedTextColour(0x5A, 0x5A, 0x5A);

/** Colour of the hint which reports a refused proxy configuration. */
const wxColour kWarningTextColour(0xB4, 0x23, 0x18);

} // namespace

NetworkPanel::NetworkPanel(wxWindow* parent, appbox::NetworkModel& model) : wxPanel(parent, wxID_ANY), model_(model)
{
    tab_bar_ = new TabBar(this, wxID_ANY);
    tab_bar_->AddTab("Proxy");
    tab_bar_->AddTab("DNS");
    tab_bar_->AddTab("IP Restrictions");
    tab_bar_->SetSelection(static_cast<int>(kStartPage));

    pages_ = new wxSimplebook(this, wxID_ANY);
    pages_->AddPage(CreateProxyPage(pages_), "Proxy");
    pages_->AddPage(CreateDnsPage(pages_), "DNS");
    pages_->AddPage(
        new PlaceholderPanel(pages_, "IP Restrictions", "Address restrictions of the packaged application."),
        "IP Restrictions");
    pages_->SetSelection(static_cast<std::size_t>(kStartPage));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tab_bar_, 0, wxEXPAND);
    sizer->Add(pages_, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(APPBOX_TAB, &NetworkPanel::OnTabChanged, this);

    RefreshList();
    RefreshProxy();
}

wxWindow* NetworkPanel::CreateProxyPage(wxWindow* parent)
{
    auto* page = new wxPanel(parent, wxID_ANY);

    proxy_type_ = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxSize(kFieldWidth, -1));
    proxy_type_->Append("SOCKS5");
    proxy_type_->SetSelection(0);
    proxy_type_->SetToolTip("Protocol of the proxy the packaged application uses");

    proxy_tcp_ = new wxCheckBox(page, wxID_ANY, "TCP");
    proxy_tcp_->SetToolTip("Send the TCP traffic of the packaged application through the proxy");

    proxy_udp_ = new wxCheckBox(page, wxID_ANY, "UDP");
    proxy_udp_->SetToolTip("Send the UDP traffic of the packaged application through the proxy");

    proxy_server_ = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(kFieldWidth, -1));
    proxy_server_->SetToolTip("Hostname or IP address of the SOCKS5 server");

    proxy_port_ = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(kPortWidth, -1));
    proxy_port_->SetMaxLength(kPortLength);
    proxy_port_->SetToolTip("Port of the SOCKS5 server, 1 to 65535");

    proxy_username_ = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(kFieldWidth, -1));
    proxy_username_->SetToolTip("User name of the SOCKS5 server; leave it empty while the server asks for none");

    proxy_password_ =
        new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(kFieldWidth, -1), wxTE_PASSWORD);
    proxy_password_->SetToolTip("Password of the SOCKS5 server; leave it empty while the server asks for none");

    auto* protocols = new wxBoxSizer(wxHORIZONTAL);
    protocols->Add(proxy_tcp_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kFieldGap);
    protocols->Add(proxy_udp_, 0, wxALIGN_CENTER_VERTICAL);

    auto* grid = new wxFlexGridSizer(2, kFieldGap, kFieldGap);
    grid->Add(new wxStaticText(page, wxID_ANY, "Proxy type:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(proxy_type_, 0);
    grid->Add(new wxStaticText(page, wxID_ANY, "Protocols:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(protocols, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(page, wxID_ANY, "Server:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(proxy_server_, 0);
    grid->Add(new wxStaticText(page, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(proxy_port_, 0);
    grid->Add(new wxStaticText(page, wxID_ANY, "User name:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(proxy_username_, 0);
    grid->Add(new wxStaticText(page, wxID_ANY, "Password:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(proxy_password_, 0);

    proxy_hint_ = new wxStaticText(page, wxID_ANY, wxEmptyString);
    proxy_hint_->SetForegroundColour(kMutedTextColour);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 0, wxALL, kFormBorder);
    sizer->Add(proxy_hint_, 0, wxLEFT | wxRIGHT | wxBOTTOM, kFormBorder);
    page->SetSizer(sizer);

    /*
     * Every control reports its change to the same handler, which offers the
     * whole form to the model.
     */
    proxy_server_->Bind(wxEVT_TEXT, &NetworkPanel::OnProxyChanged, this);
    proxy_port_->Bind(wxEVT_TEXT, &NetworkPanel::OnProxyChanged, this);
    proxy_username_->Bind(wxEVT_TEXT, &NetworkPanel::OnProxyChanged, this);
    proxy_password_->Bind(wxEVT_TEXT, &NetworkPanel::OnProxyChanged, this);
    proxy_tcp_->Bind(wxEVT_CHECKBOX, &NetworkPanel::OnProxyChanged, this);
    proxy_udp_->Bind(wxEVT_CHECKBOX, &NetworkPanel::OnProxyChanged, this);
    proxy_type_->Bind(wxEVT_CHOICE, &NetworkPanel::OnProxyChanged, this);

    return page;
}

wxWindow* NetworkPanel::CreateDnsPage(wxWindow* parent)
{
    auto* page = new wxPanel(parent, wxID_ANY);

    auto* toolbar = CreateToolBarRow(page);
    CreateList(page);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(toolbar, 0, wxEXPAND);
    sizer->Add(list_, 1, wxEXPAND);
    page->SetSizer(sizer);

    return page;
}

wxWindow* NetworkPanel::CreateToolBarRow(wxWindow* parent)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(kToolBarBackground);

    add_ = new wxButton(row, wxID_ANY, "Add...", wxDefaultPosition, wxSize(-1, 26));
    add_->SetToolTip("Add a DNS redirection");
    add_->Bind(wxEVT_BUTTON, &NetworkPanel::OnAdd, this);

    remove_ = new wxButton(row, wxID_ANY, "Remove", wxDefaultPosition, wxSize(-1, 26));
    remove_->SetToolTip("Remove the selected DNS redirection");
    remove_->Bind(wxEVT_BUTTON, &NetworkPanel::OnRemove, this);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(add_, 0, wxALL, 4);
    sizer->Add(remove_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);

    row->SetSizer(sizer);
    return row;
}

void NetworkPanel::CreateList(wxWindow* parent)
{
    list_ = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);

    /*
     * Both columns are edited inside the cell: the value of a row is entered
     * the way the table shows it, and the model is updated once the cell was
     * committed.
     */
    list_->AppendTextColumn("Hostname or IP Address", wxDATAVIEW_CELL_EDITABLE, kHostnameWidth);
    list_->AppendTextColumn("Redirect", wxDATAVIEW_CELL_EDITABLE, kRedirectWidth);

    list_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        UpdateToolBarState();
        event.Skip();
    });
    list_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &NetworkPanel::OnValueChanged, this);
}

void NetworkPanel::RefreshModel()
{
    /*
     * The draft is dropped as well: it belongs to the model which was replaced,
     * so the table shows the redirections of the new model only.
     */
    rows_.clear();
    RefreshList();
    RefreshProxy();
}

void NetworkPanel::RefreshList()
{
    /* The draft the user is filling in survives the rebuild of the table. */
    RowInfo draft;
    bool    has_draft = false;
    if (!rows_.empty() && rows_.back().pending)
    {
        draft = rows_.back();
        has_draft = true;
    }

    updating_ = true;
    list_->DeleteAllItems();

    rows_.clear();
    for (const auto& entry : model_.DnsEntries())
    {
        RowInfo row;
        row.hostname = entry.hostname;
        row.redirect = entry.redirect;
        rows_.push_back(std::move(row));
    }
    if (has_draft)
    {
        rows_.push_back(std::move(draft));
    }

    for (const auto& row : rows_)
    {
        AppendRow(row);
    }

    updating_ = false;
    UpdateToolBarState();
}

void NetworkPanel::RefreshProxy()
{
    if (proxy_type_ == nullptr)
    {
        return;
    }

    const auto& proxy = model_.Proxy();

    /*
     * Filling a control reports a change of its own, which would be read as a
     * change of the user, so the events are suppressed while the form is
     * filled.
     */
    updating_ = true;
    proxy_type_->SetSelection(0);
    proxy_tcp_->SetValue(proxy.tcp);
    proxy_udp_->SetValue(proxy.udp);
    proxy_server_->ChangeValue(wxString(proxy.server));
    proxy_port_->ChangeValue(wxString(proxy.port));
    proxy_username_->ChangeValue(wxString(proxy.username));
    proxy_password_->ChangeValue(wxString(proxy.password));
    updating_ = false;

    UpdateProxyHint(std::string());
}

void NetworkPanel::CommitProxy()
{
    if (updating_ || proxy_type_ == nullptr)
    {
        return;
    }

    appbox::ProxyConfig config;

    /* The choice holds the one protocol the workspace offers. */
    config.type = appbox::ProxyType::Socks5;
    config.tcp = proxy_tcp_->GetValue();
    config.udp = proxy_udp_->GetValue();
    config.server = proxy_server_->GetValue().ToStdWstring();
    config.port = proxy_port_->GetValue().ToStdWstring();
    config.username = proxy_username_->GetValue().ToStdWstring();
    config.password = proxy_password_->GetValue().ToStdWstring();

    std::string error;
    if (!model_.SetProxy(config, error))
    {
        /*
         * The controls keep what the user typed: the value may be incomplete
         * because it is still being typed, and the model keeps the last
         * configuration it accepted until the form holds a usable one.
         */
        UpdateProxyHint(error);
        return;
    }

    UpdateProxyHint(std::string());
}

void NetworkPanel::UpdateProxyHint(const std::string& error)
{
    if (proxy_hint_ == nullptr)
    {
        return;
    }

    if (!error.empty())
    {
        proxy_hint_->SetForegroundColour(kWarningTextColour);
        proxy_hint_->SetLabel(wxString::FromUTF8(error));
    }
    else
    {
        const auto& proxy = model_.Proxy();

        proxy_hint_->SetForegroundColour(kMutedTextColour);

        if (!proxy.tcp && !proxy.udp)
        {
            proxy_hint_->SetLabel("No proxy: the packaged application connects directly.");
        }
        else
        {
            /*
             * The summary never names the credentials, so a password cannot
             * end up on the screen of a shoulder surfer.
             */
            const wxString traffic = proxy.tcp && proxy.udp ? "The TCP and UDP traffic"
                                                            : (proxy.tcp ? "The TCP traffic" : "The UDP traffic");
            proxy_hint_->SetLabel(traffic + " of the packaged application is sent through the SOCKS5 proxy " +
                                  wxString(proxy.server) + ":" + wxString(proxy.port) + ".");
        }
    }

    proxy_hint_->GetParent()->Layout();
}

void NetworkPanel::AppendRow(const RowInfo& row)
{
    wxVector<wxVariant> values;
    values.push_back(wxVariant(wxString(row.hostname)));
    values.push_back(wxVariant(wxString(row.redirect)));
    list_->AppendItem(values);
}

void NetworkPanel::RevertCell(int row, unsigned int column, const std::wstring& value)
{
    updating_ = true;
    list_->SetTextValue(wxString(value), static_cast<unsigned>(row), column);
    updating_ = false;
}

void NetworkPanel::UpdateToolBarState()
{
    if (remove_ == nullptr || list_ == nullptr)
    {
        return;
    }

    remove_->Enable(list_->GetSelectedRow() >= 0);
}

void NetworkPanel::BeginEditRow(int row)
{
    list_->CallAfter([this, row]() {
        if (row < 0 || row >= list_->GetItemCount())
        {
            return;
        }

        list_->SelectRow(static_cast<unsigned>(row));
        list_->EditItem(list_->RowToItem(row), list_->GetColumn(kHostnameColumn));
        UpdateToolBarState();
    });
}

void NetworkPanel::ReportError(const std::string& error)
{
    wxMessageBox(wxString::FromUTF8(error), "DNS Redirection", wxOK | wxICON_ERROR, this);
}

void NetworkPanel::OnTabChanged(wxCommandEvent& event)
{
    const auto index = event.GetInt();
    if (index >= 0 && index < static_cast<int>(pages_->GetPageCount()))
    {
        pages_->SetSelection(static_cast<std::size_t>(index));
    }
    event.Skip();
}

void NetworkPanel::OnProxyChanged(wxCommandEvent& event)
{
    CommitProxy();
    event.Skip();
}

void NetworkPanel::OnAdd(wxCommandEvent&)
{
    /*
     * The table keeps at most one row which is still being filled in, so a
     * second `Add...` selects that row instead of appending another one. Its
     * editor is not opened again: the control keeps the editor it created
     * while the row is being filled in, and a second one would stay behind as
     * an unused control of the same cell.
     */
    if (!rows_.empty() && rows_.back().pending)
    {
        list_->SelectRow(static_cast<unsigned>(rows_.size()) - 1);
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
    list_->SelectRow(static_cast<unsigned>(index));
    UpdateToolBarState();
    BeginEditRow(index);
}

void NetworkPanel::OnRemove(wxCommandEvent&)
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
    if (!rows_[index].pending && !model_.RemoveDnsEntry(index))
    {
        return;
    }

    /*
     * Only the removed row is dropped. Rebuilding the whole table instead
     * would drop the item an editor of another row is bound to, and a row
     * which the user did not touch keeps its state.
     */
    updating_ = true;
    list_->DeleteItem(static_cast<unsigned>(row));
    updating_ = false;

    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(index));
    list_->UnselectAll();
    UpdateToolBarState();
}

void NetworkPanel::OnValueChanged(wxDataViewEvent& event)
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

    const auto         index = static_cast<std::size_t>(row);
    const std::wstring hostname = list_->GetTextValue(static_cast<unsigned>(row), kHostnameColumn).ToStdWstring();
    const std::wstring redirect = list_->GetTextValue(static_cast<unsigned>(row), kRedirectColumn).ToStdWstring();

    std::string error;

    if (rows_[index].pending)
    {
        /*
         * A row which is still being filled in reaches the model once both of
         * its fields carry a value. A value the model refuses keeps the draft
         * as the user typed it, because a draft holds no stored value which
         * could be put back.
         */
        if (hostname.empty() || redirect.empty())
        {
            rows_[index].hostname = hostname;
            rows_[index].redirect = redirect;
            return;
        }

        if (!model_.AddDnsEntry(hostname, redirect, error))
        {
            ReportError(error);
            return;
        }

        rows_[index].hostname = hostname;
        rows_[index].redirect = redirect;
        rows_[index].pending = false;
        return;
    }

    if (!model_.SetDnsEntry(index, hostname, redirect, error))
    {
        const auto column = static_cast<unsigned>(event.GetColumn());
        if (column == kHostnameColumn || column == kRedirectColumn)
        {
            RevertCell(row, column, column == kHostnameColumn ? rows_[index].hostname : rows_[index].redirect);
        }
        ReportError(error);
        return;
    }

    rows_[index].hostname = hostname;
    rows_[index].redirect = redirect;
}
