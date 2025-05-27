#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <vector>
#include "utils/file.hpp"
#include "utils/meta.hpp"
#include "utils/pair.hpp"
#include "Common.hpp"
#include "FileDataView.hpp"

struct FileDataViewNode
{
    FileDataViewNode();
    ~FileDataViewNode();

    FileDataViewNode*              parent;           /* Parent node. */
    std::vector<FileDataViewNode*> children;         /* Children nodes. */
    wxString                       name;             /* Node name. */
    wxString                       sandboxPath;      /* Location in sandbox. */
    wxString                       sourcePath;       /* Location in real filesystem. */
    DWORD                          dwFileAttributes; /* Entry type. */
    appbox::IsolationMode          isolation;        /* Isolation mode. */
};

struct FileDataView::Data
{
    Data(FileDataView* owner);
    ~Data();
    FileDataView*     mOwner; /* DataView owner. */
    FileDataViewNode* mRoot;  /* Root node. */
};

static appbox::Pair<const wchar_t*, appbox::IsolationMode> s_isolation_pair[] = {
    { L"Full",      appbox::ISOLATION_FULL       },
    { L"Merge",     appbox::ISOLATION_MERGE      },
    { L"WriteCopy", appbox::ISOLATION_WRITE_COPY },
    { L"Hide",      appbox::ISOLATION_HIDE       },
};

FileDataView::Filesystem::Filesystem()
{
    isolation = appbox::ISOLATION_FULL;
    dwFileAttributes = 0;
}

FileDataViewNode::FileDataViewNode()
{
    parent = nullptr;
    dwFileAttributes = 0;
    isolation = appbox::ISOLATION_MERGE;
}

FileDataViewNode::~FileDataViewNode()
{
    size_t count = children.size();
    for (size_t i = 0; i < count; i++)
    {
        delete children[i];
    }
}

static FileDataViewNode* s_new_file_entry_root()
{
    FileDataViewNode* entry = new FileDataViewNode;
    entry->name = L"Virtual Filesystem";
    return entry;
}

FileDataView::Data::Data(FileDataView* owner)
{
    mOwner = owner;
    mRoot = s_new_file_entry_root();
}

FileDataView::Data::~Data()
{
    delete mRoot;
}

FileDataView::FileDataView()
{
    mData = new Data(this);
}

FileDataView::~FileDataView()
{
    delete mData;
}

wxArrayString FileDataView::GetIsolationArray() const
{
    wxArrayString choices;
    for (size_t i = 0; i < std::size(s_isolation_pair); i++)
    {
        choices.Add(s_isolation_pair[i].k);
    }
    return choices;
}

static void s_expand_entry(FileDataViewNode* parent)
{
    auto files = appbox::ListFiles(parent->sourcePath.ToStdWstring());
    for (auto it = files.begin(); it != files.end(); it++)
    {
        FileDataViewNode* child = new FileDataViewNode;
        child->parent = parent;
        child->name = it->name;
        child->dwFileAttributes = it->dwFileAttributes;
        child->sandboxPath = parent->sandboxPath + L"\\" + child->name;
        child->sourcePath = parent->sourcePath + L"\\" + child->name;
        child->isolation = appbox::ISOLATION_FULL;
        parent->children.push_back(child);

        if (child->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            s_expand_entry(child);
        }
    }
}

static FileDataViewNode* s_build_entry_tree(const std::wstring& sandboxPath,
                                            const std::wstring& sourcePath)
{
    FileDataViewNode* root = new FileDataViewNode;
    root->parent = nullptr;
    root->name = wxFileName(sourcePath).GetFullName().ToStdWstring();
    root->dwFileAttributes = GetFileAttributesW(sourcePath.c_str());
    root->sandboxPath = sandboxPath;
    root->sourcePath = sourcePath;
    root->isolation = appbox::ISOLATION_FULL;
    if (root->dwFileAttributes == INVALID_FILE_ATTRIBUTES)
    {
        delete root;
        throw std::runtime_error("GetFileAttributesW() failed");
    }

    if (root->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        s_expand_entry(root);
    }

    return root;
}

void FileDataView::AddFolderRecursive(wxDataViewItem parent, const std::wstring& sandboxPath,
                                      const std::wstring& sourcePath)
{
    FileDataViewNode* parentEntry = mData->mRoot;
    if (parent.IsOk() && parent.GetID() != nullptr)
    {
        parentEntry = static_cast<FileDataViewNode*>(parent.GetID());
    }
    spdlog::info(L"parent: {}", parentEntry->name.ToStdWstring());

    FileDataViewNode* newEntry = s_build_entry_tree(sandboxPath, sourcePath);
    newEntry->parent = parentEntry;
    parentEntry->children.push_back(newEntry);

    // FIXME: Somehow the update does not work.
    ItemAdded(wxDataViewItem(parentEntry), wxDataViewItem(newEntry));
}

void FileDataView::DeleteItem(wxDataViewItem node)
{
    if (!node.IsOk())
    {
        return;
    }
    FileDataViewNode* entry = static_cast<FileDataViewNode*>(node.GetID());
    if (entry == nullptr || entry == mData->mRoot)
    {
        return;
    }

    FileDataViewNode* parent = entry->parent;
    wxASSERT(parent != nullptr);

    for (auto it = parent->children.begin(); it != parent->children.end(); ++it)
    {
        if (*it == entry)
        {
            parent->children.erase(it);
            break;
        }
    }

    ItemDeleted(wxDataViewItem(parent), wxDataViewItem(entry));
    delete entry;
}

bool FileDataView::IsContainer(const wxDataViewItem& item) const
{
    if (!item.IsOk())
    {
        return true;
    }

    FileDataViewNode* entry = static_cast<FileDataViewNode*>(item.GetID());
    return !entry->children.empty();
}

wxDataViewItem FileDataView::GetParent(const wxDataViewItem& item) const
{
    if (!item.IsOk())
    {
        return wxDataViewItem(0);
    }

    FileDataViewNode* entry = static_cast<FileDataViewNode*>(item.GetID());
    if (entry == mData->mRoot)
    {
        return wxDataViewItem(0);
    }

    return wxDataViewItem(entry->parent);
}

unsigned int FileDataView::GetChildren(const wxDataViewItem& item,
                                       wxDataViewItemArray&  children) const
{
    FileDataViewNode* entry = static_cast<FileDataViewNode*>(item.GetID());
    if (entry == nullptr)
    {
        children.Add(wxDataViewItem(mData->mRoot));
        return 1;
    }

    if (entry->children.empty())
    {
        return 0;
    }

    unsigned int count = entry->children.size();
    for (unsigned int i = 0; i < count; i++)
    {
        FileDataViewNode* child = entry->children[i];
        children.Add(wxDataViewItem(child));
    }

    return count;
}

static void s_col_name_get(wxVariant& variant, const FileDataViewNode* entry)
{
    variant = entry->name;
}

static bool s_col_name_set(const wxVariant& variant, FileDataViewNode* entry)
{
    entry->name = variant.GetString();
    return true;
}

static void s_col_isolation_get(wxVariant& variant, const FileDataViewNode* entry)
{
    variant = appbox::PairSearchV(s_isolation_pair, std::size(s_isolation_pair), entry->isolation);
}

static bool s_col_isolation_set(const wxVariant& variant, FileDataViewNode* entry)
{
    std::wstring isolation = variant.GetString().ToStdWstring();
    entry->isolation = appbox::PairSearchK(s_isolation_pair, std::size(s_isolation_pair),
                                           isolation.c_str(), wcscmp);
    return true;
}

/**
 * @brief Array of column-specific getter and setter functions for handling
 *        file data view values.
 *
 * This static array provides function pointers to column-specific get and
 * set operations for managing data in the FileDataView class. Each entry
 * contains a getter function to retrieve data for a specific column and
 * a setter function to update data for that column.
 *
 * @remark Each function in the array corresponds to a particular column
 *         of the file view, defining how data is accessed and modified
 *         for that column.
 */
static DataViewValueGS<FileDataViewNode> s_file_data_view_value_gs[] = {
    { s_col_name_get,      s_col_name_set      },
    { s_col_isolation_get, s_col_isolation_set },
};

void FileDataView::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    wxASSERT(item.IsOk());
    FileDataViewNode* entry = static_cast<FileDataViewNode*>(item.GetID());

    if (col >= std::size(s_file_data_view_value_gs))
    {
        wxLogError("FileDataView::GetValue() called with invalid column index");
        return;
    }

    s_file_data_view_value_gs[col].GetValue(variant, entry);
}

bool FileDataView::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    wxASSERT(item.IsOk());
    FileDataViewNode* entry = static_cast<FileDataViewNode*>(item.GetID());

    if (col >= std::size(s_file_data_view_value_gs))
    {
        wxLogError("FileDataView::SetValue() called with invalid column index");
        return false;
    }

    return s_file_data_view_value_gs[col].SetValue(variant, entry);
}

bool FileDataView::HasContainerColumns(const wxDataViewItem&) const
{
    return true;
}

static void s_node_to_fs(FileDataView::Filesystem& dst, const FileDataViewNode* src)
{
    dst.sandboxPath = src->sandboxPath.ToStdString(wxConvUTF8);
    dst.sourcePath = src->sourcePath.ToStdString(wxConvUTF8);
    dst.isolation = src->isolation;
    dst.dwFileAttributes = src->dwFileAttributes;

    for (auto it = src->children.begin(); it != src->children.end(); ++it)
    {
        FileDataView::Filesystem node;
        s_node_to_fs(node, *it);
        dst.children.push_back(node);
    }
}

std::vector<FileDataView::Filesystem> FileDataView::Export() const
{
    std::vector<FileDataView::Filesystem> result;

    for (auto it = mData->mRoot->children.begin(); it != mData->mRoot->children.end(); ++it)
    {
        FileDataView::Filesystem fs;
        s_node_to_fs(fs, *it);
        result.push_back(fs);
    }

    return result;
}

static void s_fs_to_node(FileDataViewNode* parent, const FileDataView::Filesystem& fs)
{
    FileDataViewNode* node = new FileDataViewNode;
    node->parent = parent;
    node->sandboxPath = wxString::FromUTF8(fs.sandboxPath);
    node->sourcePath = wxString::FromUTF8(fs.sourcePath);
    node->dwFileAttributes = fs.dwFileAttributes;
    node->isolation = fs.isolation;
    node->name = wxFileName(node->sourcePath).GetFullName();
    parent->children.push_back(node);

    for (auto it = fs.children.begin(); it != fs.children.end(); ++it)
    {
        s_fs_to_node(node, *it);
    }
}

void FileDataView::Import(const std::vector<FileDataView::Filesystem>& fs)
{
    delete mData->mRoot;
    mData->mRoot = s_new_file_entry_root();

    for (auto it = fs.begin(); it != fs.end(); ++it)
    {
        s_fs_to_node(mData->mRoot, *it);
    }

    Cleared();
}
