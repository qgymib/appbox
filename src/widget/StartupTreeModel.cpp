#include "StartupTreeModel.hpp"

StartupTreeModel::StartupTreeModel()
    : folder_icon_(wxArtProvider::GetBitmapBundle(wxART_FOLDER, wxART_OTHER, wxSize(16, 16))),
      executable_icon_(wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE, wxART_OTHER, wxSize(16, 16)))
{
}

void StartupTreeModel::Adopt(const std::shared_ptr<appbox::StartupTree>& tree)
{
    tree_ = tree;

    /*
     * The whole model was replaced: the control drops the rows it holds and
     * reads the new tree, which is what the scan of the dialog finishes with.
     */
    Cleared();
}

bool StartupTreeModel::IsReady() const
{
    return tree_ != nullptr;
}

std::vector<const appbox::StartupNode*> StartupTreeModel::ExpandableRows() const
{
    /* The scan did not finish yet, so the control holds no row to expand. */
    return tree_ == nullptr ? std::vector<const appbox::StartupNode*>() : tree_->ExpandableRows();
}

appbox::StartupNode* StartupTreeModel::Node(const wxDataViewItem& item) const
{
    return item.IsOk() ? static_cast<appbox::StartupNode*>(item.GetID()) : nullptr;
}

wxDataViewItem StartupTreeModel::Item(const appbox::StartupNode* node) const
{
    return node != nullptr ? wxDataViewItem(const_cast<appbox::StartupNode*>(node)) : wxDataViewItem();
}

appbox::StartupNode* StartupTreeModel::FindChoice(const appbox::StartupFile& file) const
{
    return tree_ == nullptr ? nullptr : tree_->FindChoice(file);
}

bool StartupTreeModel::HasFiles() const
{
    return tree_ != nullptr && tree_->HasFiles();
}

const std::vector<appbox::StartupFile>& StartupTreeModel::Files() const
{
    static const std::vector<appbox::StartupFile> empty;
    return tree_ == nullptr ? empty : tree_->Files();
}

bool StartupTreeModel::IsStartupFile(const wxDataViewItem& item) const
{
    const appbox::StartupNode* const node = Node(item);
    return node != nullptr && tree_ != nullptr && tree_->Contains(*node);
}

bool StartupTreeModel::IsAutoStart(const wxDataViewItem& item) const
{
    const appbox::StartupNode* const node = Node(item);
    return node != nullptr && tree_ != nullptr && tree_->IsAutoStart(*node);
}

bool StartupTreeModel::Remove(const wxDataViewItem& item)
{
    const appbox::StartupNode* const node = Node(item);
    if (node == nullptr || tree_ == nullptr || !tree_->Remove(*node))
    {
        return false;
    }

    ValueChanged(item, AutoStartColumn);
    ValueChanged(item, TriggerColumn);
    return true;
}

wxString StartupTreeModel::TakeError()
{
    wxString error;
    error.swap(error_);
    return error;
}

void StartupTreeModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    const appbox::StartupNode* const node = Node(item);
    if (node == nullptr || tree_ == nullptr)
    {
        variant = wxVariant();
        return;
    }

    /*
     * A row which cannot be a startup file keeps its cells empty: the control
     * does not render a cell without a value, so the renderer is never called
     * for it. The variant is cleared explicitly because the caller may reuse
     * it.
     */
    const bool checkable = appbox::StartupTree::IsCheckable(*node);

    switch (col)
    {
    case NameColumn:
        variant << wxDataViewIconText(node->label, IconOf(*node));
        break;
    case TypeColumn:
        variant = TypeLabel(*node);
        break;
    case AutoStartColumn:
        variant = checkable ? wxVariant(tree_->IsAutoStart(*node)) : wxVariant();
        break;
    case TriggerColumn:
        variant = checkable && tree_->Contains(*node) ? wxVariant(wxString(tree_->TriggerOf(*node))) : wxVariant();
        break;
    default:
        variant = wxVariant();
        break;
    }
}

bool StartupTreeModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    const appbox::StartupNode* const node = Node(item);
    if (node == nullptr || tree_ == nullptr || !appbox::StartupTree::IsCheckable(*node))
    {
        return false;
    }

    if (col == AutoStartColumn)
    {
        if (!tree_->SetAutoStart(*node, variant.GetBool()))
        {
            return false;
        }

        /* A row which joined the list receives its default trigger. */
        ValueChanged(item, TriggerColumn);
        return true;
    }

    if (col == TriggerColumn)
    {
        std::string error;
        if (!tree_->SetTrigger(*node, variant.GetString().ToStdWstring(), error))
        {
            error_ = wxString::FromUTF8(error);
            return false;
        }
        return true;
    }

    return false;
}

wxDataViewItem StartupTreeModel::GetParent(const wxDataViewItem& item) const
{
    const appbox::StartupNode* const node = Node(item);
    return node != nullptr ? Item(node->parent) : wxDataViewItem();
}

bool StartupTreeModel::IsContainer(const wxDataViewItem& item) const
{
    if (!item.IsOk())
    {
        return true;
    }

    const appbox::StartupNode* const node = Node(item);
    return node != nullptr && node->kind != appbox::StartupNodeKind::Executable;
}

bool StartupTreeModel::HasContainerColumns(const wxDataViewItem&) const
{
    /* The children of a container use the type and startup columns as well. */
    return true;
}

unsigned int StartupTreeModel::GetChildren(const wxDataViewItem& item, wxDataViewItemArray& children) const
{
    if (tree_ == nullptr)
    {
        /* The scan did not finish yet, so the tree has no row at all. */
        return 0;
    }

    if (!item.IsOk())
    {
        for (const auto& root : tree_->Roots())
        {
            children.Add(Item(root.get()));
        }
        return static_cast<unsigned int>(children.GetCount());
    }

    appbox::StartupNode* const node = Node(item);
    if (node == nullptr || !IsContainer(item))
    {
        return 0;
    }

    for (const auto& child : node->children)
    {
        children.Add(Item(child.get()));
    }
    return static_cast<unsigned int>(children.GetCount());
}

const wxBitmapBundle& StartupTreeModel::IconOf(const appbox::StartupNode& node) const
{
    return node.kind == appbox::StartupNodeKind::Executable ? executable_icon_ : folder_icon_;
}

wxString StartupTreeModel::TypeLabel(const appbox::StartupNode& node)
{
    return node.kind == appbox::StartupNodeKind::Executable ? "Executable" : "Folder";
}
