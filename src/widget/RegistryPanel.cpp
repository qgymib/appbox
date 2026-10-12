#include "RegistryPanel.hpp"
#include "RegistryIsolationDialog.hpp"
#include "RegistryKeyDialog.hpp"
#include "RegistryValueDialog.hpp"
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/imaglist.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <utility>

namespace
{

/** Minimum width of the tree pane. */
constexpr int kTreePaneWidth = 280;

/** Model column of the isolation dropdown. */
constexpr unsigned int kIsolationColumn = 1;

/** Background of the toolbar row above the table. */
const wxColour kToolBarBackground(0xF2, 0xF3, 0xF5);

/** Maximum number of characters shown by the value column. */
constexpr std::size_t kValuePreviewLength = 120;

/** Lead of the tooltip of the header of the `Isolation` column. */
const char* const kIsolationColumnLead =
    "Isolation mode of the entry, which decides what the application sees and where its modifications land.\n"
    "A key entry covers the key and everything below it, a value entry covers the value of that name.";

/**
 * @brief Describe the modes the `Isolation` column offers.
 *
 * A key and a value carry different modes, so the description names the modes
 * of both kinds of row.
 *
 * @return The description of the column.
 */
wxString IsolationColumnTooltip()
{
    wxString text = kIsolationColumnLead;

    text += "\n\nA key offers:";
    for (const auto isolation : { appbox::RegistryKeyIsolation::Full, appbox::RegistryKeyIsolation::Merge,
                                  appbox::RegistryKeyIsolation::WriteCopy, appbox::RegistryKeyIsolation::Hide })
    {
        text += "\n\n";
        text += wxString(appbox::RegistryKeyIsolationDescription(isolation));
    }

    text += "\n\nA value offers:";
    for (const auto isolation : { appbox::RegistryValueIsolation::Full, appbox::RegistryValueIsolation::Hide })
    {
        text += "\n\n";
        text += wxString(appbox::RegistryValueIsolationDescription(isolation));
    }

    return text;
}

/**
 * @brief Renderer of the `Isolation` column.
 *
 * The column offers the isolation modes of a row, and the modes of a key differ
 * from the modes of a value, so the choices depend on the row and cannot be the
 * fixed list of a choice renderer. The renderer draws the mode of the row as
 * text and asks the panel for the modes which fit the row when the cell is
 * activated.
 */
class IsolationRenderer : public wxDataViewCustomRenderer
{
public:
    /**
     * @brief Create the renderer of a panel.
     * @param[in] panel The panel which owns the table and the rows.
     */
    explicit IsolationRenderer(RegistryPanel* panel)
        : wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_ACTIVATABLE, wxALIGN_LEFT), panel_(panel)
    {
    }

    /**
     * @brief Store the text of the cell.
     * @param[in] value Text of the mode of the row.
     * @return true.
     */
    bool SetValue(const wxVariant& value) override
    {
        text_ = value.GetString();
        return true;
    }

    /**
     * @brief Report the text of the cell.
     * @param[out] value Text of the mode of the row.
     * @return true.
     */
    bool GetValue(wxVariant& value) const override
    {
        value = text_;
        return true;
    }

    /**
     * @brief Get the size the cell needs.
     * @return The size of the text plus the room of the dropdown marker.
     */
    wxSize GetSize() const override
    {
        return GetTextExtent(text_) + wxSize(20, 4);
    }

    /**
     * @brief Draw the mode of the row.
     * @param[in] rect Rectangle of the cell.
     * @param[in] dc Device context to draw into.
     * @param[in] state State of the cell.
     * @return true.
     */
    bool Render(wxRect rect, wxDC* dc, int state) override
    {
        RenderText(text_, 0, rect, dc, state);
        return true;
    }

    /**
     * @brief Offer the modes which fit the row.
     *
     * The model, the column and the mouse event of the call are not used: the
     * panel resolves the row of the item and draws the popup itself.
     *
     * @param[in] cell Rectangle of the cell.
     * @param[in] item Item of the row.
     * @return true when a mode was chosen and applied.
     */
    bool ActivateCell(const wxRect& cell, wxDataViewModel*, const wxDataViewItem& item, unsigned int,
                      const wxMouseEvent*) override
    {
        return panel_ != nullptr && panel_->PopupIsolationMenu(item, cell);
    }

private:
    /** @brief The panel which owns the table and the rows. */
    RegistryPanel* panel_ = nullptr;

    /** @brief Text of the cell, the name of the mode of the row. */
    wxString text_;
};

/**
 * @brief Get the name shown by the name column of a row.
 * @param[in] row Row to format.
 * @return The label of the row.
 */
wxString RowLabel(const std::wstring& name, bool is_value)
{
    if (is_value && name.empty())
    {
        return "(Default)";
    }
    return wxString(name);
}

/**
 * @brief Get the command id of the isolation menu item.
 *
 * The id is created once, so every popup of the tree shares it without
 * colliding with the ids of the standard commands.
 *
 * @return The command id of the menu item.
 */
int IsolationMenuId()
{
    static const int id = wxNewId();
    return id;
}

} // namespace

RegistryPanel::RegistryPanel(wxWindow* parent, appbox::RegistryModel& model) : wxPanel(parent, wxID_ANY), model_(model)
{
    auto* splitter =
        new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
    splitter->SetMinimumPaneSize(180);

    auto folder_icon = wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));
    auto open_icon = wxArtProvider::GetBitmap(wxART_FOLDER_OPEN, wxART_OTHER, wxSize(16, 16));
    auto images = new wxImageList(16, 16);
    images->Add(folder_icon);
    images->Add(open_icon);

    /*
     * The root item stays visible: the top of the registry view is the
     * `Sandbox Registry` container, which the user selects to reach the root
     * keys, so the tree must not hide it.
     */
    tree_ = new wxTreeCtrl(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
    tree_->AssignImageList(images);

    /*
     * The Name column of the table carries an icon as well: a folder for a sub
     * key row and a plain file for a value row. Both icons come from the art
     * provider, so the table never reads the icon of a host registry entry.
     * The image list above belongs to the tree and is unrelated to them.
     */
    folder_icon_ = wxArtProvider::GetBitmapBundle(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));
    file_icon_ = wxArtProvider::GetBitmapBundle(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16));

    auto* right = new wxPanel(splitter, wxID_ANY);
    CreateList(right);

    auto* right_sizer = new wxBoxSizer(wxVERTICAL);
    right_sizer->Add(CreateToolBarRow(right), 0, wxEXPAND);
    right_sizer->Add(list_, 1, wxEXPAND);
    right->SetSizer(right_sizer);

    splitter->SplitVertically(tree_, right, kTreePaneWidth);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(splitter, 1, wxEXPAND);
    SetSizer(sizer);

    tree_->Bind(wxEVT_TREE_SEL_CHANGED, &RegistryPanel::OnTreeSelectionChanged, this);
    tree_->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, &RegistryPanel::OnTreeRightClick, this);

    BuildTree();
    SelectTreePath(std::wstring());
    RefreshList();
}

void RegistryPanel::CreateList(wxWindow* parent)
{
    list_ = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
    /*
     * The Name column shows an icon before the name of the row: a folder for a
     * sub key and a plain file for a value, so the kind of a row is visible
     * without reading the type column. Its values are icon-text variants, see
     * AppendRow().
     */
    list_->AppendIconTextColumn("Name", wxDATAVIEW_CELL_INERT, 260);

    /*
     * The isolation column offers the modes which fit the kind of a row: a key
     * carries the four modes of a key and a value the two modes of a value, so
     * the column uses a renderer which asks the panel for the choices of the row
     * it draws. The renderer is added through AppendColumn() because it has to
     * claim the model column of the table explicitly.
     */
    list_->AppendColumn(new wxDataViewColumn("Isolation", new IsolationRenderer(this), kIsolationColumn, 110));

    list_->AppendTextColumn("Type", wxDATAVIEW_CELL_INERT, 140);
    list_->AppendTextColumn("Value", wxDATAVIEW_CELL_INERT, 300);

    list_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &RegistryPanel::OnItemActivated, this);
    list_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        UpdateToolBarState();
        event.Skip();
    });

    /*
     * The header of the table describes the modes of the `Isolation` column, see
     * DataViewTooltip; the cells of the table carry no tooltip of their own.
     */
    tooltip_ = std::make_unique<DataViewTooltip>(
        *list_, [](int, int) { return wxString(); }, [this](int column) { return TooltipForHeader(column); });
}

wxString RegistryPanel::TooltipForHeader(int column) const
{
    if (column == static_cast<int>(kIsolationColumn))
    {
        return IsolationColumnTooltip();
    }

    return {};
}

wxWindow* RegistryPanel::CreateToolBarRow(wxWindow* parent)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(kToolBarBackground);

    add_key_ = new wxButton(row, wxID_ANY, "Add key", wxDefaultPosition, wxSize(-1, 26));
    add_key_->SetToolTip("Add a sub key to the selected key");
    add_key_->Bind(wxEVT_BUTTON, &RegistryPanel::OnAddKey, this);

    add_value_ = new wxButton(row, wxID_ANY, "Add value", wxDefaultPosition, wxSize(-1, 26));
    add_value_->SetToolTip("Add a value to the selected key");
    add_value_->Bind(wxEVT_BUTTON, &RegistryPanel::OnAddValue, this);

    remove_ = new wxButton(row, wxID_ANY, "Remove", wxDefaultPosition, wxSize(-1, 26));
    remove_->SetToolTip("Remove the selected sub key or value");
    remove_->Bind(wxEVT_BUTTON, &RegistryPanel::OnRemove, this);

    up_dir_ = new wxButton(row, wxID_ANY, "Up Dir", wxDefaultPosition, wxSize(-1, 26));
    up_dir_->SetToolTip("Move the tree selection to the parent key");
    up_dir_->Bind(wxEVT_BUTTON, &RegistryPanel::OnUpDir, this);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(add_key_, 0, wxALL, 4);
    sizer->Add(add_value_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->Add(remove_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->Add(up_dir_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->AddStretchSpacer();

    row->SetSizer(sizer);
    return row;
}

void RegistryPanel::RefreshModel()
{
    BuildTree();
    SelectTreePath(current_path_);
    RefreshList();
}

void RegistryPanel::BuildTree()
{
    updating_ = true;

    tree_->DeleteAllItems();

    const auto root = tree_->AddRoot(wxString(appbox::kRegistryContainerLabel), 0, 1, new TreeNode());
    AddKeyNodes(root, std::wstring(), model_.Root());
    tree_->Expand(root);

    /* The five root keys are opened, so their content is visible at a glance. */
    wxTreeItemIdValue cookie = nullptr;
    for (auto item = tree_->GetFirstChild(root, cookie); item.IsOk(); item = tree_->GetNextChild(root, cookie))
    {
        tree_->Expand(item);
    }

    updating_ = false;
}

void RegistryPanel::AddKeyNodes(const wxTreeItemId& parent_item, const std::wstring& parent_path,
                                const appbox::RegistryKeyNode& key)
{
    for (const auto& child : key.children)
    {
        const auto path = appbox::JoinRegistryPath(parent_path, child.name);

        auto* data = new TreeNode();
        data->path = path;

        const auto item = tree_->AppendItem(parent_item, wxString(child.name), 0, 1, data);
        AddKeyNodes(item, path, child);
    }
}

wxTreeItemId RegistryPanel::FindTreeItem(const wxTreeItemId& parent_item, const std::wstring& path) const
{
    if (!parent_item.IsOk())
    {
        return {};
    }

    auto* data = static_cast<TreeNode*>(tree_->GetItemData(parent_item));
    if (data != nullptr && appbox::RegistryPathEquals(data->path, path))
    {
        return parent_item;
    }

    wxTreeItemIdValue cookie = nullptr;
    for (auto child = tree_->GetFirstChild(parent_item, cookie); child.IsOk();
         child = tree_->GetNextChild(parent_item, cookie))
    {
        const auto found = FindTreeItem(child, path);
        if (found.IsOk())
        {
            return found;
        }
    }
    return {};
}

void RegistryPanel::SelectTreePath(const std::wstring& path)
{
    const auto root = tree_->GetRootItem();
    const auto item = FindTreeItem(root, path);
    if (item.IsOk())
    {
        tree_->SelectItem(item);
        tree_->EnsureVisible(item);
        return;
    }

    if (root.IsOk())
    {
        tree_->SelectItem(root);
    }
}

std::wstring RegistryPanel::SelectedTreePath() const
{
    const auto selection = tree_->GetSelection();
    if (!selection.IsOk())
    {
        return {};
    }

    auto* data = static_cast<TreeNode*>(tree_->GetItemData(selection));
    return data != nullptr ? data->path : std::wstring();
}

void RegistryPanel::RefreshList()
{
    rows_.clear();

    for (const auto& row : model_.Rows(current_path_))
    {
        RowInfo info;
        info.kind = row.kind == appbox::RegistryRow::Kind::Key ? RowInfo::Kind::Key : RowInfo::Kind::Value;
        info.name = row.name;
        info.key_isolation = row.key_isolation;
        info.value_isolation = row.value_isolation;
        info.type = row.type;
        info.data = row.data;
        rows_.push_back(std::move(info));
    }

    updating_ = true;
    list_->DeleteAllItems();
    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        AppendRow(rows_[index], index);
    }
    updating_ = false;

    UpdateToolBarState();
}

void RegistryPanel::AppendRow(const RowInfo& row, std::size_t index)
{
    const bool is_value = row.kind == RowInfo::Kind::Value;

    /*
     * The name column carries the icon of the row as well, so its value is an
     * icon-text variant. The icon-text class declares no implicit variant
     * constructor, its value is assigned through the stream operator.
     */
    wxVariant name;
    name << wxDataViewIconText(RowLabel(row.name, is_value), IconOf(row));

    /*
     * The mode of a row comes from the list which fits its kind: a value carries
     * the two value modes alone.
     */
    const wxString isolation = is_value ? wxString(appbox::RegistryValueIsolationName(row.value_isolation))
                                        : wxString(appbox::RegistryKeyIsolationName(row.key_isolation));

    wxVector<wxVariant> values;
    values.push_back(name);
    values.push_back(wxVariant(isolation));
    values.push_back(wxVariant(is_value ? wxString(appbox::RegistryValueTypeName(row.type)) : wxString()));
    values.push_back(wxVariant(
        is_value ? wxString(appbox::FormatRegistryValueData(row.type, row.data, kValuePreviewLength)) : wxString()));

    list_->AppendItem(values, static_cast<wxUIntPtr>(index));
}

const wxBitmapBundle& RegistryPanel::IconOf(const RowInfo& row) const
{
    return row.kind == RowInfo::Kind::Key ? folder_icon_ : file_icon_;
}

void RegistryPanel::UpdateToolBarState()
{
    /* The container holds the root keys only, so it accepts neither of them. */
    const bool has_key = !current_path_.empty();

    if (add_value_ != nullptr)
    {
        add_value_->Enable(has_key);
    }
    if (add_key_ != nullptr)
    {
        add_key_->Enable(has_key);
    }
    if (up_dir_ != nullptr)
    {
        /*
         * The container is the root of the tree and the only node without a
         * parent, so the command is offered for every node but the container: a
         * root key moves to the container like a sub key moves to the key it
         * hangs below.
         */
        up_dir_->Enable(has_key);
    }

    if (remove_ == nullptr)
    {
        return;
    }

    bool      removable = false;
    const int index = SelectedRowIndex();
    if (index >= 0)
    {
        const auto& row = rows_[static_cast<std::size_t>(index)];
        if (row.kind == RowInfo::Kind::Value)
        {
            removable = true;
        }
        else
        {
            const auto* key = model_.FindKey(appbox::JoinRegistryPath(current_path_, row.name));
            removable = key != nullptr && key->removable;
        }
    }
    remove_->Enable(removable);
}

int RegistryPanel::RowIndex(const wxDataViewItem& item) const
{
    if (!item.IsOk())
    {
        return -1;
    }

    const auto data = list_->GetItemData(item);
    if (data >= rows_.size())
    {
        return -1;
    }
    return static_cast<int>(data);
}

int RegistryPanel::SelectedRowIndex() const
{
    const int selected = list_->GetSelectedRow();
    if (selected == wxNOT_FOUND)
    {
        return -1;
    }
    return RowIndex(list_->RowToItem(selected));
}

void RegistryPanel::ApplyIsolation(const RowInfo& row, appbox::RegistryKeyIsolation isolation)
{
    /*
     * The mode of a row reaches the row itself: the sub keys and the values
     * below it keep their modes. The subtree of a key is only overwritten
     * through the isolation dialog of the tree.
     */
    model_.SetKeyIsolation(appbox::JoinRegistryPath(current_path_, row.name), isolation);
    RefreshList();
}

void RegistryPanel::ApplyIsolation(const RowInfo& row, appbox::RegistryValueIsolation isolation)
{
    model_.SetValueIsolation(current_path_, row.name, isolation);
    RefreshList();
}

bool RegistryPanel::PopupIsolationMenu(const wxDataViewItem& item, const wxRect& cell)
{
    const int index = RowIndex(item);
    if (index < 0)
    {
        return false;
    }

    const RowInfo row = rows_[static_cast<std::size_t>(index)];
    const bool    is_value = row.kind == RowInfo::Kind::Value;
    const auto&   names = is_value ? appbox::RegistryValueIsolationNames() : appbox::RegistryKeyIsolationNames();

    wxMenu menu;
    int    chosen = -1;
    for (std::size_t position = 0; position < names.size(); ++position)
    {
        const int id = wxWindow::NewControlId();
        menu.Append(id, wxString(names[position]));
        menu.Bind(wxEVT_MENU, [&chosen, position](wxCommandEvent&) { chosen = static_cast<int>(position); }, id);
    }

    list_->PopupMenu(&menu, list_->ClientToScreen(cell.GetTopLeft()));
    if (chosen < 0)
    {
        return false;
    }

    /*
     * The table is rebuilt once the control finished the activation; doing it
     * inside the handler would delete the row the control still holds while it
     * processes the click.
     */
    if (is_value)
    {
        CallAfter([this, row, chosen]() { ApplyIsolation(row, static_cast<appbox::RegistryValueIsolation>(chosen)); });
    }
    else
    {
        CallAfter([this, row, chosen]() { ApplyIsolation(row, static_cast<appbox::RegistryKeyIsolation>(chosen)); });
    }

    return true;
}

void RegistryPanel::EditKey(int index)
{
    const auto row = rows_[static_cast<std::size_t>(index)];
    const auto path = appbox::JoinRegistryPath(current_path_, row.name);

    /*
     * The root keys are fixed, so renaming them is not offered at all. The
     * model refuses the rename as well, which keeps the rule enforced even
     * when this guard is bypassed.
     */
    const auto* key = model_.FindKey(path);
    if (key != nullptr && !key->removable)
    {
        wxMessageBox("The root keys of the registry view cannot be renamed.", "Edit Key", wxOK | wxICON_INFORMATION,
                     this);
        return;
    }

    /*
     * The dialog validates against a copy of the model, so the rules of the
     * model decide whether a name is accepted without being duplicated here
     * and without a rejected name changing the registry.
     */
    RegistryKeyDialog dialog(this, "Edit Key", wxString(current_path_), wxString(row.name), row.key_isolation,
                             [this, path](const std::wstring& name) -> std::string {
                                 appbox::RegistryModel copy = model_;
                                 std::string           error;
                                 if (!copy.RenameKey(path, name, error))
                                 {
                                     return error;
                                 }
                                 return {};
                             });
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!model_.RenameKey(path, dialog.Name(), error))
    {
        wxMessageBox(wxString::FromUTF8(error), "Edit Key", wxOK | wxICON_ERROR, this);
        return;
    }

    /*
     * The isolation mode of the dialog reaches the renamed key as well: the
     * rename only changes the name, so the mode is stored under the new path.
     */
    if (!model_.SetKeyIsolation(appbox::JoinRegistryPath(current_path_, dialog.Name()), dialog.Isolation()))
    {
        wxMessageBox("The isolation mode of the renamed key could not be stored.", "Edit Key", wxOK | wxICON_ERROR,
                     this);
    }

    BuildTree();
    SelectTreePath(current_path_);
    RefreshList();
}

void RegistryPanel::EditValue(int index)
{
    const auto row = rows_[static_cast<std::size_t>(index)];
    const auto key_path = current_path_;
    const auto old_name = row.name;

    RegistryValueDialog dialog(this, "Edit Value", wxString(key_path), wxString(old_name), row.type, row.data,
                               row.value_isolation,
                               [this, key_path, old_name](const std::wstring& name, appbox::RegistryValueType type,
                                                          const std::vector<std::uint8_t>& data) -> std::string {
                                   appbox::RegistryModel copy = model_;
                                   std::string           error;
                                   if (!copy.UpdateValue(key_path, old_name, name, type, data, error))
                                   {
                                       return error;
                                   }
                                   return {};
                               });
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!model_.UpdateValue(key_path, old_name, dialog.Name(), dialog.Type(), dialog.Data(), error))
    {
        wxMessageBox(wxString::FromUTF8(error), "Edit Value", wxOK | wxICON_ERROR, this);
        return;
    }

    /*
     * The isolation mode of the dialog reaches the edited value as well: the
     * update only changes the name, the type and the data, so the mode is
     * stored under the name the dialog reports.
     */
    if (!model_.SetValueIsolation(key_path, dialog.Name(), dialog.Isolation()))
    {
        wxMessageBox("The isolation mode of the edited value could not be stored.", "Edit Value", wxOK | wxICON_ERROR,
                     this);
    }

    RefreshList();
}

void RegistryPanel::OnTreeSelectionChanged(wxTreeEvent& event)
{
    if (!updating_)
    {
        current_path_ = SelectedTreePath();
        RefreshList();
    }
    event.Skip();
}

void RegistryPanel::OnTreeRightClick(wxTreeEvent& event)
{
    const auto item = event.GetItem();
    if (!item.IsOk())
    {
        return;
    }

    /*
     * The menu works on the key under the cursor, so it becomes the selection
     * and the table below the tree shows its content.
     */
    tree_->SelectItem(item);

    const auto path = SelectedTreePath();
    if (path.empty())
    {
        /* The container holds the root keys only, so it carries no mode. */
        return;
    }

    wxMenu menu;
    menu.Append(IsolationMenuId(), "Isolation Mode...");
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) { EditIsolation(path); }, IsolationMenuId());
    PopupMenu(&menu);
}

void RegistryPanel::EditIsolation(const std::wstring& path)
{
    const auto* key = model_.FindKey(path);
    if (key == nullptr)
    {
        return;
    }

    RegistryIsolationDialog dialog(this, wxString(path), key->isolation);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    if (dialog.Recursive())
    {
        model_.ApplyIsolationToSubtree(path, dialog.Isolation());
    }
    else
    {
        /* Without the recursion the mode reaches the key alone. */
        model_.SetKeyIsolation(path, dialog.Isolation());
    }

    BuildTree();
    SelectTreePath(path);
    RefreshList();
}

void RegistryPanel::OnItemActivated(wxDataViewEvent& event)
{
    const int index = RowIndex(event.GetItem());
    if (index < 0)
    {
        return;
    }

    if (rows_[static_cast<std::size_t>(index)].kind == RowInfo::Kind::Key)
    {
        EditKey(index);
    }
    else
    {
        EditValue(index);
    }
}

void RegistryPanel::OnAddKey(wxCommandEvent&)
{
    if (current_path_.empty())
    {
        return;
    }

    /*
     * A new key follows the key it is created in, so the mode the dialog offers
     * starts at the mode of the selected key.
     */
    const auto* parent = model_.FindKey(current_path_);
    const auto  initial_isolation = parent != nullptr ? parent->isolation : appbox::RegistryKeyIsolation::Merge;

    RegistryKeyDialog dialog(this, "Add Key", wxString(current_path_), wxEmptyString, initial_isolation,
                             [this](const std::wstring& name) -> std::string {
                                 appbox::RegistryModel copy = model_;
                                 std::string           error;
                                 if (!copy.AddKey(current_path_, name, error))
                                 {
                                     return error;
                                 }
                                 return {};
                             });
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!model_.AddKey(current_path_, dialog.Name(), error))
    {
        wxMessageBox(wxString::FromUTF8(error), "Add Key", wxOK | wxICON_ERROR, this);
        return;
    }

    /* The mode the user picked replaces the mode the new key inherited. */
    if (!model_.SetKeyIsolation(appbox::JoinRegistryPath(current_path_, dialog.Name()), dialog.Isolation()))
    {
        wxMessageBox("The isolation mode of the new key could not be stored.", "Add Key", wxOK | wxICON_ERROR, this);
    }

    BuildTree();
    SelectTreePath(current_path_);
    RefreshList();
}

void RegistryPanel::OnAddValue(wxCommandEvent&)
{
    if (current_path_.empty())
    {
        return;
    }

    /*
     * A new value follows the key it is created in, so the mode the dialog
     * offers starts at the mode DefaultValueIsolation() reports for the
     * selected key.
     */
    const auto* parent = model_.FindKey(current_path_);
    const auto  initial_isolation =
        appbox::DefaultValueIsolation(parent != nullptr ? parent->isolation : appbox::RegistryKeyIsolation::Merge);

    RegistryValueDialog dialog(this, "Add Value", wxString(current_path_), wxEmptyString,
                               appbox::RegistryValueType::String, {}, initial_isolation,
                               [this](const std::wstring& name, appbox::RegistryValueType type,
                                      const std::vector<std::uint8_t>& data) -> std::string {
                                   appbox::RegistryModel copy = model_;
                                   std::string           error;
                                   if (!copy.AddValue(current_path_, name, type, data, error))
                                   {
                                       return error;
                                   }
                                   return {};
                               });
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!model_.AddValue(current_path_, dialog.Name(), dialog.Type(), dialog.Data(), error))
    {
        wxMessageBox(wxString::FromUTF8(error), "Add Value", wxOK | wxICON_ERROR, this);
        return;
    }

    /* The mode the user picked replaces the mode the new value inherited. */
    if (!model_.SetValueIsolation(current_path_, dialog.Name(), dialog.Isolation()))
    {
        wxMessageBox("The isolation mode of the new value could not be stored.", "Add Value", wxOK | wxICON_ERROR,
                     this);
    }

    RefreshList();
}

void RegistryPanel::OnRemove(wxCommandEvent&)
{
    const int index = SelectedRowIndex();
    if (index < 0)
    {
        return;
    }

    const auto row = rows_[static_cast<std::size_t>(index)];

    if (row.kind == RowInfo::Kind::Value)
    {
        model_.RemoveValue(current_path_, row.name);
        RefreshList();
        return;
    }

    const auto  path = appbox::JoinRegistryPath(current_path_, row.name);
    const auto* key = model_.FindKey(path);
    if (key == nullptr || !key->removable)
    {
        wxMessageBox("The root keys of the registry view cannot be removed.", "Remove", wxOK | wxICON_INFORMATION,
                     this);
        return;
    }

    const auto      question = wxString::Format("Remove the key '%s' and everything below it?", wxString(row.name));
    wxMessageDialog confirm(this, question, "Remove", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    if (confirm.ShowModal() != wxID_YES)
    {
        return;
    }

    std::string error;
    if (!model_.RemoveKey(path, error))
    {
        wxMessageBox(wxString::FromUTF8(error), "Remove", wxOK | wxICON_ERROR, this);
        return;
    }

    BuildTree();
    SelectTreePath(current_path_);
    RefreshList();
}

void RegistryPanel::OnUpDir(wxCommandEvent&)
{
    /*
     * The rule of the command is the one of the toolbar state: the container is
     * the root of the tree and the only node without a parent, so every other
     * key moves to the key the tree shows above it. The path of the selection
     * names that key, which keeps the command on the model and not on the shape
     * of the tree; the selection change refreshes the table.
     */
    if (current_path_.empty())
    {
        return;
    }

    SelectTreePath(appbox::RegistryParentPath(current_path_));
}
