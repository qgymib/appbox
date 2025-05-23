#ifndef APPBOX_WIDGETS_COMMON_HPP
#define APPBOX_WIDGETS_COMMON_HPP

#include <wx/wx.h>

enum AppboxID
{
    APPBOX_LOWEST = wxID_HIGHEST + 1,
    APPBOX_PACKER_MAINFRAME_PROCESS_BUTTON,
    APPBOX_PACKER_MAINFRAME_STARTUP_FILES_BUTTON,
};

template<typename T>
struct DataViewValueGS
{
    void (*GetValue)(wxVariant& variant, const T* entry);
    bool (*SetValue)(const wxVariant& variant, T* entry);
};

#endif
