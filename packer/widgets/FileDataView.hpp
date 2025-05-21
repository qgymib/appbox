#ifndef APPBOX_PACKER_WIDGETS_FILE_DATA_VIEW_HPP
#define APPBOX_PACKER_WIDGETS_FILE_DATA_VIEW_HPP

#include <wx/dataview.h>
#include <nlohmann/json.hpp>
#include <vector>
#include "utils/meta.hpp"

class FileDataView : public wxDataViewModel
{
public:
    struct Filesystem
    {
        std::wstring            sandboxPath;      /* Path in sandbox. */
        std::wstring            sourcePath;       /* Path to copy from real filesystem. */
        appbox::IsolationMode   isolation;        /* Isolation. */
        DWORD                   dwFileAttributes; /* File Attribute.*/
        std::vector<Filesystem> children;         /* Children nodes. */

        Filesystem();
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Filesystem, sandboxPath, sourcePath, isolation,
                                                    dwFileAttributes, children);
    };

public:
    FileDataView();
    ~FileDataView() override;

    /**
     * Retrieves an array containing the isolation string values.
     * @return An array containing the isolation string values.
     */
    wxArrayString GetIsolationArray() const;

    /**
     * Adds the contents of a folder and its subfolders recursively to the data model,
     * under a specified parent item.
     *
     * @param[in] parent The parent item under which the folder structure will be added.
     *               If invalid, the root node is assumed.
     * @param[in] sandboxPath The path of the folder in the sandbox environment.
     * @param[in] sourcePath The source path of the folder to be added.
     */
    void AddFolderRecursive(wxDataViewItem parent, const std::wstring& sandboxPath,
                            const std::wstring& sourcePath);

    void DeleteItem(wxDataViewItem node);

    std::vector<Filesystem> Export() const;
    void Import(const std::vector<Filesystem>& fs);

    /* Override */
public:
    bool           IsContainer(const wxDataViewItem& item) const override;
    wxDataViewItem GetParent(const wxDataViewItem& item) const override;
    unsigned int   GetChildren(const wxDataViewItem& item,
                               wxDataViewItemArray&  children) const override;
    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;
    bool HasContainerColumns(const wxDataViewItem&) const override;

private:
    struct Data;
    Data* mData;
};

#endif
