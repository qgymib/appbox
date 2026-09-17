#ifndef APPBOX_LOADER_WIDGET_REGISTRY_BROWSER_HPP
#define APPBOX_LOADER_WIDGET_REGISTRY_BROWSER_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>
#include <string>
#include <vector>
#include "../registry/HiveReader.hpp"

/**
 * @brief Read-only browser of the sandbox registry hive.
 *
 * The layout mirrors the Windows registry editor: a key tree on the left, the
 * value list (name / type / data) of the selected key on the right and the
 * full path of the selected key in a bar above both. Only the sandbox hive is
 * shown, never the host registry.
 */
class RegistryBrowser : public wxPanel
{
public:
    /**
     * @brief Create the browser.
     * @param[in] parent The parent window.
     * @param[in] hive_file The DOS path of the sandbox hive file.
     */
    RegistryBrowser(wxWindow* parent, const std::wstring& hive_file);

private:
    /**
     * @brief Tree item data which carries the key path relative to the hive root.
     */
    class KeyItemData : public wxTreeItemData
    {
    public:
        /**
         * @brief Bind a key path to a tree item.
         * @param[in] relative The key path relative to the hive root.
         */
        explicit KeyItemData(const std::wstring& relative) : relative(relative)
        {
        }

        /**
         * @brief The key path relative to the hive root, empty for the root item.
         */
        std::wstring relative;
    };

    /**
     * @brief Rebuild the whole tree from a fresh hive mount.
     *
     * The previously selected key is restored when it still exists, otherwise
     * the root is selected.
     * @param[in] restore_path The key path to select after the rebuild.
     */
    void RebuildTree(const std::wstring& restore_path);

    /**
     * @brief Insert the sub keys of a tree item, replacing the placeholder child.
     * @param[in] item The tree item to populate.
     */
    void PopulateChildren(const wxTreeItemId& item);

    /**
     * @brief Show the values of a key in the list.
     * @param[in] relative The key path relative to the hive root.
     */
    void ShowValues(const std::wstring& relative);

    /**
     * @brief Open the read-only detail dialog of a list row.
     * @param[in] index The row index of the value.
     */
    void ShowValueDetail(long index);

    /**
     * @brief The relative path of the currently selected tree item.
     * @return The path, empty for the root or when nothing is selected.
     */
    std::wstring SelectedRelativePath() const;

    /**
     * @brief Build the full display path of a key.
     * @param[in] relative The key path relative to the hive root.
     * @return The path below the root label, for example
     *         HKEY_CURRENT_USER\Software\Vendor.
     */
    std::wstring DisplayPath(const std::wstring& relative) const;

    /**
     * @brief Show the values of the selected key.
     */
    void OnTreeSelChanged(wxTreeEvent& event);

    /**
     * @brief Populate a tree item which is about to expand.
     */
    void OnTreeItemExpanding(wxTreeEvent& event);

    /**
     * @brief Remount the hive and rebuild the tree (Refresh button, F5).
     */
    void OnRefresh(wxCommandEvent& event);

    /**
     * @brief Open the detail dialog of the activated list row.
     */
    void OnListItemActivated(wxListEvent& event);

    appbox::HiveReader           reader_;         /* Hive mount of the browser. */
    std::vector<appbox::RegistryValue> values_;   /* Values of the selected key. */
    wxTextCtrl*                  path_bar_ = nullptr; /* Bar with the selected key path. */
    wxTreeCtrl*                  tree_ = nullptr; /* Key tree. */
    wxListCtrl*                  list_ = nullptr; /* Value list. */
    int                          icon_closed_ = -1; /* Image list index of a collapsed key. */
    int                          icon_open_ = -1; /* Image list index of an expanded key. */
};

#endif // APPBOX_LOADER_WIDGET_REGISTRY_BROWSER_HPP
