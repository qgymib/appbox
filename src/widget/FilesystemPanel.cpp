#include "FilesystemPanel.hpp"
#include "core/PresetDirectory.hpp"
#include "WString.hpp"
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/imaglist.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/srchctrl.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <system_error>

namespace
{

/** Context menu command: import a host folder below a preset directory. */
const int kMenuImportFolder = wxNewId();

/** Context menu command: remove an imported folder. */
const int kMenuRemoveImport = wxNewId();

/** Minimum width of the tree pane. */
constexpr int kTreePaneWidth = 260;

/** Background of the toolbar row above the file list. */
const wxColour kToolBarBackground(0xF2, 0xF3, 0xF5);

/**
 * @brief Case insensitive wide string comparison.
 * @param[in] a Left operand.
 * @param[in] b Right operand.
 * @return true when both strings are equal ignoring case.
 */
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::towlower(a[i]) != std::towlower(b[i]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Format a byte count for the size column.
 * @param[in] size Size in bytes.
 * @return Human readable size.
 */
wxString FormatSize(std::uintmax_t size)
{
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };

    auto value = static_cast<double>(size);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0)
    {
        return wxString::Format("%u B", static_cast<unsigned>(size));
    }
    return wxString::Format("%.1f %s", value, units[unit]);
}

/**
 * @brief Read the size of a host file for the size column.
 * @param[in] path Host path.
 * @return Human readable size, "-" when the file cannot be measured.
 */
wxString HostEntrySize(const std::wstring& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return "-";
    }
    return FormatSize(size);
}

/**
 * @brief Compose the virtual path shown in the Source Path column.
 * @param[in] preset Preset directory owning the entry.
 * @param[in] target_dir Directory relative to the preset directory.
 * @param[in] name Entry name.
 * @return The virtual path inside the sandbox view.
 */
wxString VirtualPath(const appbox::PresetDirectory& preset, const std::wstring& target_dir,
                     const std::wstring& name)
{
    std::wstring path = preset.layer_key;
    if (!target_dir.empty())
    {
        path += L"\\";
        path += target_dir;
    }
    if (!name.empty())
    {
        path += L"\\";
        path += name;
    }
    return path;
}

/**
 * @brief Compare two host paths ignoring case.
 * @param[in] a Left path.
 * @param[in] b Right path.
 * @return true when both paths describe the same file.
 */
bool SameHostPath(const std::wstring& a, const std::wstring& b)
{
    return EqualsIgnoreCase(std::filesystem::path(a).lexically_normal().wstring(),
                            std::filesystem::path(b).lexically_normal().wstring());
}

} // namespace

FilesystemPanel::FilesystemPanel(wxWindow* parent, appbox::PackModel& model)
    : wxPanel(parent, wxID_ANY),
      model_(model)
{
    auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxSP_LIVE_UPDATE | wxSP_3DSASH);
    splitter->SetMinimumPaneSize(180);

    auto folder_icon = wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));
    auto open_icon = wxArtProvider::GetBitmap(wxART_FOLDER_OPEN, wxART_OTHER, wxSize(16, 16));
    auto images = new wxImageList(16, 16);
    images->Add(folder_icon);
    images->Add(open_icon);

    tree_ = new wxTreeCtrl(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_HIDE_ROOT);
    tree_->AssignImageList(images);

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

    tree_->Bind(wxEVT_TREE_SEL_CHANGED, &FilesystemPanel::OnTreeSelectionChanged, this);
    tree_->Bind(wxEVT_TREE_ITEM_EXPANDING, &FilesystemPanel::OnTreeItemExpanding, this);
    tree_->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, &FilesystemPanel::OnTreeItemContextMenu, this);
    Bind(wxEVT_MENU, &FilesystemPanel::OnAddFolder, this, kMenuImportFolder);
    Bind(wxEVT_MENU, &FilesystemPanel::OnRemoveImportFromTree, this, kMenuRemoveImport);

    BuildTree();
}

void FilesystemPanel::CreateList(wxWindow* parent)
{
    list_ = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_ROW_LINES | wxDV_SINGLE);
    list_->AppendTextColumn("Filename", wxDATAVIEW_CELL_INERT, 220);
    list_->AppendTextColumn("Isolation", wxDATAVIEW_CELL_INERT, 76);
    list_->AppendToggleColumn("Hidden", wxDATAVIEW_CELL_INERT, 60);
    list_->AppendToggleColumn("No Sync", wxDATAVIEW_CELL_INERT, 64);
    list_->AppendToggleColumn("Read Only", wxDATAVIEW_CELL_INERT, 74);
    list_->AppendToggleColumn("No Upgrade", wxDATAVIEW_CELL_INERT, 80);
    list_->AppendTextColumn("Size", wxDATAVIEW_CELL_INERT, 78, wxALIGN_RIGHT);
    list_->AppendTextColumn("Source Path", wxDATAVIEW_CELL_INERT, 300);

    list_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        UpdateToolBarState();
        event.Skip();
    });
}

wxWindow* FilesystemPanel::CreateToolBarRow(wxWindow* parent)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(kToolBarBackground);

    add_files_ = new wxButton(row, wxID_ANY, "Add Files", wxDefaultPosition, wxSize(-1, 26));
    add_files_->SetToolTip("Import individual files into the selected folder");
    add_files_->Bind(wxEVT_BUTTON, &FilesystemPanel::OnAddFiles, this);

    add_folder_ = new wxButton(row, wxID_ANY, "Add Folder", wxDefaultPosition, wxSize(-1, 26));
    add_folder_->SetToolTip("Import a host folder below the selected preset directory");
    add_folder_->Bind(wxEVT_BUTTON, &FilesystemPanel::OnAddFolder, this);

    auto* new_folder = new wxButton(row, wxID_ANY, "New Folder", wxDefaultPosition, wxSize(-1, 26));
    new_folder->SetToolTip("Reserved: the packer imports existing host folders only");
    new_folder->Enable(false);

    remove_ = new wxButton(row, wxID_ANY, "Remove", wxDefaultPosition, wxSize(-1, 26));
    remove_->SetToolTip("Remove the selected imported folder or imported file");
    remove_->Bind(wxEVT_BUTTON, &FilesystemPanel::OnRemove, this);

    up_dir_ = new wxButton(row, wxID_ANY, "Up Dir", wxDefaultPosition, wxSize(-1, 26));
    up_dir_->SetToolTip("Move the tree selection to the parent folder");
    up_dir_->Bind(wxEVT_BUTTON, &FilesystemPanel::OnUpDir, this);

    search_ = new wxSearchCtrl(row, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(180, -1));
    search_->ShowCancelButton(true);
    search_->SetDescriptiveText("Search");
    search_->Bind(wxEVT_TEXT, &FilesystemPanel::OnSearch, this);
    search_->Bind(wxEVT_SEARCH_CANCEL, &FilesystemPanel::OnSearch, this);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(add_files_, 0, wxALL, 4);
    sizer->Add(add_folder_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->Add(new_folder, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->Add(remove_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->Add(up_dir_, 0, wxTOP | wxBOTTOM | wxRIGHT, 4);
    sizer->AddStretchSpacer();
    sizer->Add(search_, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);

    row->SetSizer(sizer);
    return row;
}

void FilesystemPanel::RefreshModel()
{
    BuildTree();
    RefreshList();
}

void FilesystemPanel::BuildTree()
{
    tree_->DeleteAllItems();

    const auto root = tree_->AddRoot("Filesystem");

    for (const auto& preset : appbox::PresetDirectories())
    {
        auto* data = new TreeNode();
        data->preset_id = preset.id;

        const auto item = tree_->AppendItem(root, preset.display_name, 0, -1, data);
        for (const auto& imported : model_.ImportsOf(preset.id))
        {
            auto* import_data = new TreeNode();
            import_data->preset_id = preset.id;
            import_data->import_name = imported.import_name;

            const auto import_item = tree_->AppendItem(item, imported.import_name, 1, -1, import_data);
            tree_->SetItemHasChildren(import_item);
        }
        tree_->Expand(item);
    }

    wxTreeItemIdValue cookie = nullptr;
    const auto first = tree_->GetFirstChild(root, cookie);
    if (first.IsOk())
    {
        tree_->SelectItem(first);
    }
}

void FilesystemPanel::PopulateNode(const wxTreeItemId& item)
{
    auto* node = static_cast<TreeNode*>(tree_->GetItemData(item));
    if (node == nullptr || node->import_name.empty() || node->populated)
    {
        return;
    }
    node->populated = true;

    appbox::ImportedFolder imported;
    if (!model_.GetImport(node->preset_id, node->import_name, imported))
    {
        return;
    }

    const auto folder = node->relative_dir.empty()
                            ? std::filesystem::path(imported.source_path)
                            : std::filesystem::path(imported.source_path) / node->relative_dir;

    std::vector<std::wstring> subfolders;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(folder, ec);
         it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_directory(ec) && !ec)
        {
            subfolders.push_back(it->path().filename().wstring());
        }
    }

    std::sort(subfolders.begin(), subfolders.end());
    for (const auto& name : subfolders)
    {
        auto* child = new TreeNode();
        child->preset_id = node->preset_id;
        child->import_name = node->import_name;
        child->relative_dir =
            node->relative_dir.empty() ? name : node->relative_dir + L"\\" + name;

        const auto child_item = tree_->AppendItem(item, name, 0, -1, child);
        tree_->SetItemHasChildren(child_item);
    }
}

void FilesystemPanel::RefreshList()
{
    rows_.clear();

    const auto selection = tree_->GetSelection();
    auto* node = selection.IsOk() ? static_cast<TreeNode*>(tree_->GetItemData(selection)) : nullptr;
    if (node != nullptr)
    {
        if (node->import_name.empty())
        {
            ListPresetImports(*node);
        }
        else
        {
            ListFolderContent(*node);
        }
    }

    ApplyFilter();
    UpdateToolBarState();
}

void FilesystemPanel::ListPresetImports(const TreeNode& node)
{
    for (const auto& imported : model_.ImportsOf(node.preset_id))
    {
        RowInfo row;
        row.kind = RowInfo::Kind::ImportedFolder;
        row.preset_id = node.preset_id;
        row.import_name = imported.import_name;
        row.target_dir = imported.import_name;
        row.file_name = imported.import_name;
        row.source_path = imported.source_path;
        row.is_directory = true;
        rows_.push_back(std::move(row));
    }
}

void FilesystemPanel::ListFolderContent(const TreeNode& node)
{
    appbox::PresetDirectory preset;
    if (!appbox::FindPresetDirectory(node.preset_id, preset))
    {
        return;
    }

    appbox::ImportedFolder imported;
    if (!model_.GetImport(node.preset_id, node.import_name, imported))
    {
        return;
    }

    std::wstring target_dir = node.import_name;
    auto folder = std::filesystem::path(imported.source_path);
    if (!node.relative_dir.empty())
    {
        target_dir += L"\\" + node.relative_dir;
        folder /= node.relative_dir;
    }

    std::wstring main_path;
    const bool has_main = model_.MainProgramPath(main_path);

    /* Folders first, then files, both ordered by name. */
    std::vector<std::pair<std::wstring, bool>> entries;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(folder, ec);
         it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            break;
        }

        const bool is_directory = it->is_directory(ec);
        if (ec)
        {
            continue;
        }
        entries.emplace_back(it->path().filename().wstring(), is_directory);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second)
        {
            return left.second;
        }
        return left.first < right.first;
    });

    for (const auto& entry : entries)
    {
        RowInfo row;
        row.kind = RowInfo::Kind::HostEntry;
        row.preset_id = node.preset_id;
        row.import_name = node.import_name;
        row.target_dir = target_dir;
        row.file_name = entry.first;
        row.host_path = (folder / entry.first).wstring();
        row.is_directory = entry.second;
        row.is_main_program = has_main && !entry.second && SameHostPath(row.host_path, main_path);
        rows_.push_back(std::move(row));
    }

    /* Files which were imported on their own are listed after the host entries. */
    for (const auto& file : model_.FilesOf(node.preset_id, target_dir))
    {
        RowInfo row;
        row.kind = RowInfo::Kind::ImportedFile;
        row.preset_id = node.preset_id;
        row.import_name = node.import_name;
        row.target_dir = target_dir;
        row.file_name = file.file_name;
        row.source_path = file.source_path;
        row.is_main_program = has_main && SameHostPath(file.source_path, main_path);
        rows_.push_back(std::move(row));
    }
}

void FilesystemPanel::ApplyFilter()
{
    const auto filter = search_ != nullptr ? search_->GetValue().Lower() : wxString();

    list_->DeleteAllItems();
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        const auto name = wxString(rows_[i].file_name);
        if (!filter.empty() && name.Lower().Find(filter) == wxNOT_FOUND)
        {
            continue;
        }
        AppendRow(rows_[i], i);
    }
}

void FilesystemPanel::AppendRow(const RowInfo& row, std::size_t index)
{
    appbox::PresetDirectory preset;
    if (!appbox::FindPresetDirectory(row.preset_id, preset))
    {
        return;
    }

    wxString size = "-";
    if (row.kind == RowInfo::Kind::ImportedFile)
    {
        size = HostEntrySize(row.source_path);
    }
    else if (row.kind == RowInfo::Kind::HostEntry && !row.is_directory)
    {
        size = HostEntrySize(row.host_path);
    }

    wxVector<wxVariant> values;
    values.push_back(wxVariant(wxString(row.file_name)));
    values.push_back(wxVariant(wxString("Full")));
    values.push_back(wxVariant(false));
    values.push_back(wxVariant(false));
    values.push_back(wxVariant(false));
    values.push_back(wxVariant(false));
    values.push_back(wxVariant(size));
    values.push_back(wxVariant(VirtualPath(preset, row.target_dir, row.file_name)));

    list_->AppendItem(values, static_cast<wxUIntPtr>(index));
}

void FilesystemPanel::SelectNode(const std::string& preset_id, const std::wstring& import_name)
{
    const auto root = tree_->GetRootItem();

    wxTreeItemIdValue preset_cookie = nullptr;
    for (auto item = tree_->GetFirstChild(root, preset_cookie); item.IsOk();
         item = tree_->GetNextChild(root, preset_cookie))
    {
        auto* node = static_cast<TreeNode*>(tree_->GetItemData(item));
        if (node == nullptr || node->preset_id != preset_id)
        {
            continue;
        }
        if (import_name.empty())
        {
            tree_->SelectItem(item);
            return;
        }

        wxTreeItemIdValue child_cookie = nullptr;
        for (auto child = tree_->GetFirstChild(item, child_cookie); child.IsOk();
             child = tree_->GetNextChild(item, child_cookie))
        {
            auto* child_node = static_cast<TreeNode*>(tree_->GetItemData(child));
            if (child_node != nullptr && EqualsIgnoreCase(child_node->import_name, import_name))
            {
                tree_->SelectItem(child);
                return;
            }
        }
        tree_->SelectItem(item);
        return;
    }
}

bool FilesystemPanel::SelectedTarget(std::string& preset_id, std::wstring& target_dir,
                                     std::wstring& import_name) const
{
    const auto selection = tree_->GetSelection();
    auto* node = selection.IsOk() ? static_cast<TreeNode*>(tree_->GetItemData(selection)) : nullptr;
    if (node == nullptr || node->import_name.empty())
    {
        return false;
    }

    preset_id = node->preset_id;
    import_name = node->import_name;
    target_dir = node->relative_dir.empty() ? node->import_name
                                            : node->import_name + L"\\" + node->relative_dir;
    return true;
}

int FilesystemPanel::SelectedRowIndex() const
{
    const auto selected = list_->GetSelectedRow();
    if (selected == wxNOT_FOUND)
    {
        return -1;
    }

    const auto index = list_->GetItemData(list_->RowToItem(selected));
    if (index >= rows_.size())
    {
        return -1;
    }
    return static_cast<int>(index);
}

void FilesystemPanel::UpdateToolBarState()
{
    const auto selection = tree_->GetSelection();
    auto* node = selection.IsOk() ? static_cast<TreeNode*>(tree_->GetItemData(selection)) : nullptr;

    const bool has_node = node != nullptr;
    const bool inside_import = has_node && !node->import_name.empty();

    if (add_files_ != nullptr)
    {
        add_files_->Enable(inside_import);
    }
    if (add_folder_ != nullptr)
    {
        add_folder_->Enable(has_node);
    }
    if (remove_ != nullptr)
    {
        remove_->Enable(SelectedRowIndex() >= 0);
    }
    if (up_dir_ != nullptr)
    {
        const auto parent = selection.IsOk() ? tree_->GetItemParent(selection) : wxTreeItemId();
        up_dir_->Enable(parent.IsOk() && parent != tree_->GetRootItem());
    }
}

void FilesystemPanel::OnTreeSelectionChanged(wxTreeEvent& event)
{
    RefreshList();
    event.Skip();
}

void FilesystemPanel::OnTreeItemExpanding(wxTreeEvent& event)
{
    PopulateNode(event.GetItem());
    event.Skip();
}

void FilesystemPanel::OnTreeItemContextMenu(wxTreeEvent& event)
{
    const auto item = event.GetItem();
    auto* node = item.IsOk() ? static_cast<TreeNode*>(tree_->GetItemData(item)) : nullptr;
    if (node == nullptr)
    {
        return;
    }

    tree_->SelectItem(item);

    wxMenu menu;
    if (node->import_name.empty())
    {
        menu.Append(kMenuImportFolder, "Import Folder...");
    }
    else
    {
        menu.Append(kMenuRemoveImport, "Remove Import");
    }
    PopupMenu(&menu);
}

void FilesystemPanel::OnAddFiles(wxCommandEvent&)
{
    std::string preset_id;
    std::wstring target_dir;
    std::wstring import_name;
    if (!SelectedTarget(preset_id, target_dir, import_name))
    {
        return;
    }

    wxFileDialog dialog(this, "Select the files to import into '" + wxString(import_name) + "'",
                        wxEmptyString, wxEmptyString, "All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    wxArrayString paths;
    dialog.GetPaths(paths);

    std::vector<std::wstring> sources;
    sources.reserve(paths.GetCount());
    for (const auto& path : paths)
    {
        sources.push_back(path.ToStdWstring());
    }

    std::string error;
    if (!model_.ImportFiles(preset_id, target_dir, sources, error))
    {
        wxMessageBox(error, "Add Files", wxOK | wxICON_ERROR, this);
        return;
    }

    RefreshList();
}

void FilesystemPanel::OnAddFolder(wxCommandEvent&)
{
    const auto selection = tree_->GetSelection();
    auto* node = selection.IsOk() ? static_cast<TreeNode*>(tree_->GetItemData(selection)) : nullptr;
    if (node == nullptr)
    {
        return;
    }

    const auto preset_id = node->preset_id;

    wxDirDialog dialog(this, "Select the folder to import", wxEmptyString,
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
    {
        return;
    }

    std::string error;
    if (!model_.ImportFolder(preset_id, dialog.GetPath().ToStdWstring(), error))
    {
        wxMessageBox(error, "Add Folder", wxOK | wxICON_ERROR, this);
        return;
    }

    BuildTree();
    SelectNode(preset_id, std::wstring());
    RefreshList();
}

void FilesystemPanel::OnRemove(wxCommandEvent&)
{
    const auto index = SelectedRowIndex();
    if (index < 0)
    {
        return;
    }

    const auto row = rows_[static_cast<std::size_t>(index)];
    if (row.kind == RowInfo::Kind::HostEntry)
    {
        wxMessageBox("'" + wxString(row.file_name)
                         + "' belongs to the imported folder and is removed together with it.",
                     "Remove", wxOK | wxICON_INFORMATION, this);
        return;
    }

    const bool is_folder = row.kind == RowInfo::Kind::ImportedFolder;
    const auto question =
        is_folder ? wxString::Format("Remove the imported folder '%s' and everything below it?",
                                     row.file_name)
                  : wxString::Format("Remove the imported file '%s'?", row.file_name);

    wxMessageDialog dialog(this, question, "Remove", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    if (dialog.ShowModal() != wxID_YES)
    {
        return;
    }

    if (is_folder)
    {
        model_.RemoveImport(row.preset_id, row.import_name);
        BuildTree();
        SelectNode(row.preset_id, std::wstring());
        RefreshList();
        return;
    }

    model_.RemoveImportedFile(row.preset_id, row.target_dir, row.file_name);
    RefreshList();
}

void FilesystemPanel::OnRemoveImportFromTree(wxCommandEvent&)
{
    const auto selection = tree_->GetSelection();
    auto* node = selection.IsOk() ? static_cast<TreeNode*>(tree_->GetItemData(selection)) : nullptr;
    if (node == nullptr || node->import_name.empty())
    {
        return;
    }

    const auto question = wxString::Format("Remove the imported folder '%s' and everything below it?",
                                           node->import_name);
    wxMessageDialog dialog(this, question, "Remove Import", wxYES_NO | wxNO_DEFAULT
                                                                   | wxICON_QUESTION);
    if (dialog.ShowModal() != wxID_YES)
    {
        return;
    }

    const auto preset_id = node->preset_id;
    model_.RemoveImport(preset_id, node->import_name);

    BuildTree();
    SelectNode(preset_id, std::wstring());
    RefreshList();
}

void FilesystemPanel::OnUpDir(wxCommandEvent&)
{
    const auto selection = tree_->GetSelection();
    if (!selection.IsOk())
    {
        return;
    }

    const auto parent = tree_->GetItemParent(selection);
    if (parent.IsOk() && parent != tree_->GetRootItem())
    {
        tree_->SelectItem(parent);
    }
}

void FilesystemPanel::OnSearch(wxCommandEvent& event)
{
    ApplyFilter();
    event.Skip();
}
