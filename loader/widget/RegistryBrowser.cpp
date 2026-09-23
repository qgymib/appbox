#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/accel.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include "RegistryBrowser.hpp"

namespace
{

/**
 * @brief Label of the tree root, the container of the five root keys.
 *
 * The hive holds one sub key per root key of the view, so the tree of the
 * browser mirrors the layout of the registry editor below the container.
 */
const wchar_t* kRootLabel = L"Sandbox Registry";

/**
 * @brief Label of the default value row of the value list.
 */
const wchar_t* kDefaultLabel = L"(Default)";

/**
 * @brief Placeholder text of a value without data.
 */
const wchar_t* kNotSetLabel = L"(value not set)";

/**
 * @brief Message shown when the sandbox has not created the hive yet.
 */
const wchar_t* kMissingHiveMessage = L"The sandbox registry hive has not been created yet.";

/**
 * @brief Compare two value names case insensitively.
 * @param[in] a The left name.
 * @param[in] b The right name.
 * @return true when a sorts before b.
 */
bool ValueNameLess(const appbox::RegistryValue& a, const appbox::RegistryValue& b)
{
    return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
}

} // namespace

RegistryBrowser::RegistryBrowser(wxWindow* parent, const std::wstring& hive_file) : wxPanel(parent, wxID_ANY)
{
    reader_.Open(hive_file);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    /* Minimal toolbar with the refresh action, F5 is bound as accelerator. */
    auto* toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    toolbar->SetToolBitmapSize(wxSize(16, 16));
    toolbar->AddTool(wxID_REFRESH, "Refresh", wxArtProvider::GetBitmap(wxART_REFRESH, wxART_TOOLBAR, wxSize(16, 16)),
                     "Refresh the sandbox registry (F5)");
    toolbar->Realize();
    sizer->Add(toolbar, 0, wxEXPAND);

    /* Bar with the full path of the selected key. */
    path_bar_ = new wxTextCtrl(this, wxID_ANY, kRootLabel, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    path_bar_->SetMargins(4, 3);
    sizer->Add(path_bar_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

    /* Main area: key tree on the left, value list on the right. */
    auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    splitter->SetMinimumPaneSize(120);

    tree_ = new wxTreeCtrl(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE);

    auto* images = new wxImageList(16, 16);
    icon_closed_ = images->Add(wxArtProvider::GetIcon(wxART_FOLDER, wxART_MENU, wxSize(16, 16)));
    icon_open_ = images->Add(wxArtProvider::GetIcon(wxART_FOLDER_OPEN, wxART_MENU, wxSize(16, 16)));
    tree_->SetImageList(images);

    list_ = new wxListCtrl(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->AppendColumn("Name", wxLIST_FORMAT_LEFT, 200);
    list_->AppendColumn("Type", wxLIST_FORMAT_LEFT, 110);
    list_->AppendColumn("Data", wxLIST_FORMAT_LEFT, 420);

    splitter->SplitVertically(tree_, list_, 260);
    sizer->Add(splitter, 1, wxEXPAND | wxALL, 4);

    SetSizer(sizer);

    /*
     * Bind the interactions: tree expansion populates lazily, tree selection
     * refreshes the value list, a list activation opens the detail dialog and
     * the refresh action remounts the hive.
     */
    tree_->Bind(wxEVT_TREE_ITEM_EXPANDING, &RegistryBrowser::OnTreeItemExpanding, this);
    tree_->Bind(wxEVT_TREE_SEL_CHANGED, &RegistryBrowser::OnTreeSelChanged, this);
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &RegistryBrowser::OnListItemActivated, this);
    Bind(wxEVT_MENU, &RegistryBrowser::OnRefresh, this, wxID_REFRESH);

    wxAcceleratorEntry entries[] = {wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F5, wxID_REFRESH)};
    SetAcceleratorTable(wxAcceleratorTable(1, entries));

    RebuildTree(L"");
}

void RegistryBrowser::RebuildTree(const std::wstring& restore_path)
{
    tree_->DeleteAllItems();

    auto root = tree_->AddRoot(kRootLabel, icon_closed_, icon_open_, new KeyItemData(L""));
    if (reader_.IsOpen() && reader_.HasSubKeys(L""))
    {
        /* Placeholder child, replaced by the real sub keys on expansion. */
        tree_->AppendItem(root, L"");
    }

    /*
     * Restore the selection of the previous view when the key still exists,
     * otherwise the root is selected. The parents of the restored key are
     * populated so the path is visible.
     */
    auto selection = root;
    if (!restore_path.empty())
    {
        std::wstring current;
        auto         item = root;
        const auto   parts = restore_path; /* Split below component by component. */
        size_t       begin = 0;
        bool         broken = false;
        while (begin < parts.size() && !broken)
        {
            size_t end = parts.find(L'\\', begin);
            if (end == std::wstring::npos)
            {
                end = parts.size();
            }
            const auto component = parts.substr(begin, end - begin);
            begin = end + 1;

            current = current.empty() ? component : current + L"\\" + component;
            PopulateChildren(item);

            bool found = false;
            wxTreeItemIdValue cookie;
            for (auto child = tree_->GetFirstChild(item, cookie); child.IsOk();
                 child = tree_->GetNextChild(item, cookie))
            {
                auto* data = dynamic_cast<KeyItemData*>(tree_->GetItemData(child));
                if (data != nullptr && _wcsicmp(data->relative.c_str(), current.c_str()) == 0)
                {
                    item   = child;
                    found  = true;
                    break;
                }
            }
            if (!found)
            {
                broken = true;
            }
        }
        if (!broken)
        {
            selection = item;
        }
    }

    tree_->SelectItem(selection);
    tree_->EnsureVisible(selection);
    ShowValues(SelectedRelativePath());
}

void RegistryBrowser::PopulateChildren(const wxTreeItemId& item)
{
    /*
     * A not yet expanded item only holds the placeholder child without item
     * data. Populate the item when such a child exists, load the sub keys and
     * give every sub key a placeholder again when it holds sub keys itself.
     */
    wxTreeItemIdValue cookie;
    bool              needs_population = false;
    for (auto child = tree_->GetFirstChild(item, cookie); child.IsOk(); child = tree_->GetNextChild(item, cookie))
    {
        if (tree_->GetItemData(child) == nullptr)
        {
            needs_population = true;
            break;
        }
    }
    if (!needs_population)
    {
        return;
    }

    tree_->DeleteChildren(item);

    auto* data = dynamic_cast<KeyItemData*>(tree_->GetItemData(item));
    if (data == nullptr)
    {
        return;
    }

    std::vector<std::wstring> names;
    if (!reader_.EnumSubKeys(data->relative, names))
    {
        return;
    }

    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    for (const auto& name : names)
    {
        const auto relative = data->relative.empty() ? name : data->relative + L"\\" + name;
        auto       child    = tree_->AppendItem(item, name, icon_closed_, icon_open_, new KeyItemData(relative));
        if (reader_.HasSubKeys(relative))
        {
            tree_->AppendItem(child, L"");
        }
    }
}

void RegistryBrowser::ShowValues(const std::wstring& relative)
{
    list_->DeleteAllItems();
    values_.clear();

    if (!reader_.IsOpen())
    {
        if (reader_.IsMissing())
        {
            const auto index = list_->InsertItem(list_->GetItemCount(), kMissingHiveMessage);
            list_->SetItemFont(index, list_->GetFont().Italic());
        }
        return;
    }

    std::vector<appbox::RegistryValue> values;
    if (!reader_.EnumValues(relative, values))
    {
        return;
    }

    /* The default value is always the first row, the rest sorts by name. */
    const auto def = std::find_if(values.begin(), values.end(),
                                  [](const appbox::RegistryValue& v) { return v.name.empty(); });
    if (def == values.end())
    {
        appbox::RegistryValue none;
        none.type = REG_SZ;
        values_.push_back(std::move(none));
    }
    else
    {
        values_.push_back(std::move(*def));
        values.erase(def);
    }

    std::sort(values.begin(), values.end(), ValueNameLess);
    for (auto& value : values)
    {
        values_.push_back(std::move(value));
    }

    constexpr size_t kMaxDataChars = 512;
    for (size_t i = 0; i < values_.size(); ++i)
    {
        const auto& value = values_[i];

        auto text = value.name.empty() ? std::wstring(kDefaultLabel) : value.name;
        const auto index = list_->InsertItem(list_->GetItemCount(), text);

        list_->SetItem(index, 1, appbox::FormatValueTypeName(value.type));

        auto data = value.name.empty() && value.data.empty() && value.type == REG_SZ
                        ? std::wstring(kNotSetLabel)
                        : appbox::FormatValueData(value, kMaxDataChars);
        list_->SetItem(index, 2, data);

        list_->SetItemData(index, static_cast<long>(i));
    }
}

void RegistryBrowser::ShowValueDetail(long index)
{
    if (index < 0 || static_cast<size_t>(index) >= values_.size())
    {
        return;
    }

    const auto& value = values_[static_cast<size_t>(index)];

    wxDialog dlg(this, wxID_ANY, "Value");
    auto*     sizer = new wxBoxSizer(wxVERTICAL);

    auto AddRow = [&dlg, sizer](const wxString& label, const wxString& content, bool multiline) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(&dlg, wxID_ANY, label), 0, wxALIGN_TOP | wxRIGHT, 8);

        auto* text = new wxTextCtrl(&dlg, wxID_ANY, content, wxDefaultPosition,
                                    multiline ? wxSize(480, 220) : wxSize(480, -1),
                                    wxTE_READONLY | (multiline ? wxTE_MULTILINE : 0));
        if (multiline)
        {
            auto font = text->GetFont();
            font.SetFamily(wxFONTFAMILY_TELETYPE);
            text->SetFont(font);
        }
        row->Add(text, 1, wxEXPAND);
        sizer->Add(row, multiline ? 1 : 0, wxEXPAND | wxALL, 4);
    };

    AddRow("Name:", value.name.empty() ? kDefaultLabel : value.name, false);
    AddRow("Type:", appbox::FormatValueTypeName(value.type), false);

    /* The detail view shows the complete data, never the truncated column text. */
    auto data = appbox::FormatValueData(value, 0);
    switch (value.type)
    {
    case REG_SZ:
    case REG_EXPAND_SZ:
    case REG_MULTI_SZ:
    case REG_DWORD:
    case REG_QWORD:
        break;
    default:
        if (!value.data.empty())
        {
            data += L"\r\n\r\n";
            data += appbox::FormatHexDump(value.data);
        }
        break;
    }
    AddRow("Data:", data, true);

    sizer->Add(dlg.CreateButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 4);
    dlg.SetSizerAndFit(sizer);
    dlg.CentreOnParent();
    dlg.ShowModal();
}

std::wstring RegistryBrowser::SelectedRelativePath() const
{
    auto item = tree_->GetSelection();
    if (!item.IsOk())
    {
        return L"";
    }

    auto* data = dynamic_cast<KeyItemData*>(tree_->GetItemData(item));
    return data == nullptr ? L"" : data->relative;
}

std::wstring RegistryBrowser::DisplayPath(const std::wstring& relative) const
{
    if (relative.empty())
    {
        return kRootLabel;
    }
    return std::wstring(kRootLabel) + L"\\" + relative;
}

void RegistryBrowser::OnTreeSelChanged(wxTreeEvent& event)
{
    event.Skip();

    const auto relative = SelectedRelativePath();
    path_bar_->ChangeValue(DisplayPath(relative));
    ShowValues(relative);
}

void RegistryBrowser::OnTreeItemExpanding(wxTreeEvent& event)
{
    event.Skip();

    PopulateChildren(event.GetItem());
}

void RegistryBrowser::OnRefresh(wxCommandEvent&)
{
    const auto previous = SelectedRelativePath();
    if (!reader_.Refresh() && !reader_.IsMissing())
    {
        wxLogError("Failed to remount the sandbox registry hive.");
        return;
    }
    RebuildTree(previous);
}

void RegistryBrowser::OnListItemActivated(wxListEvent& event)
{
    ShowValueDetail(event.GetIndex());
}
