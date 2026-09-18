#include "StartupTreeModel.hpp"

StartupTreeModel::StartupTreeModel(const appbox::PackModel& model)
    : tree_(model),
      folder_icon_(wxArtProvider::GetBitmapBundle(wxART_FOLDER, wxART_OTHER, wxSize(16, 16))),
      executable_icon_(wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE, wxART_OTHER, wxSize(16, 16)))
{
}

appbox::StartupNode* StartupTreeModel::Node(const wxDataViewItem& item) const
{
    return item.IsOk() ? static_cast<appbox::StartupNode*>(item.GetID()) : nullptr;
}

wxDataViewItem StartupTreeModel::Item(const appbox::StartupNode* node) const
{
    return node != nullptr ? wxDataViewItem(const_cast<appbox::StartupNode*>(node)) : wxDataViewItem();
}

appbox::StartupNode* StartupTreeModel::FindChoice(const appbox::MainProgram& choice)
{
    return tree_.FindChoice(choice);
}

void StartupTreeModel::Preselect(const appbox::MainProgram& choice)
{
    tree_.Preselect(choice);

    /*
     * The lookup creates the folders on the way to the file, so the row can be
     * expanded by the dialog afterwards.
     */
    checked_item_ = Item(tree_.FindChoice(choice));
}

bool StartupTreeModel::SetChecked(const wxDataViewItem& item)
{
    return ChangeValue(wxVariant(true), item, StartupColumn);
}

bool StartupTreeModel::HasChecked() const
{
    return tree_.HasChecked();
}

appbox::MainProgram StartupTreeModel::Checked() const
{
    return tree_.Checked();
}

void StartupTreeModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    const appbox::StartupNode* const node = Node(item);
    if (node == nullptr)
    {
        return;
    }

    switch (col)
    {
    case NameColumn:
        variant << wxDataViewIconText(node->label, IconOf(*node));
        break;
    case TypeColumn:
        variant = TypeLabel(*node);
        break;
    case StartupColumn:
        /*
         * A row which cannot be the startup file keeps its cell empty: the
         * control does not render a cell without a value, so the renderer is
         * never called for it. The variant is cleared explicitly because the
         * caller may reuse it.
         */
        variant = appbox::StartupTree::IsCheckable(*node) ? wxVariant(tree_.IsChecked(*node))
                                                          : wxVariant();
        break;
    default:
        variant = wxVariant();
        break;
    }
}

bool StartupTreeModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    if (col != StartupColumn)
    {
        return false;
    }

    const appbox::StartupNode* const node = Node(item);
    if (node == nullptr)
    {
        return false;
    }

    const wxDataViewItem previous = checked_item_;
    if (variant.GetBool())
    {
        if (!tree_.SetChecked(*node))
        {
            return false;
        }
        checked_item_ = item;
    }
    else
    {
        tree_.ClearChecked();
        checked_item_ = wxDataViewItem();
    }

    /* The row which was checked before has to be repainted as well. */
    if (previous.IsOk() && previous != item)
    {
        ValueChanged(previous, col);
    }
    return true;
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
    if (!item.IsOk())
    {
        for (const auto& root : tree_.Roots())
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

    tree_.EnsureChildren(*node);
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
