#ifndef APPBOX_LOADER_WIDGET_MAIN_FRAME_HPP
#define APPBOX_LOADER_WIDGET_MAIN_FRAME_HPP

#include <wx/wx.h>
#include <string>

class MainFrame : public wxFrame
{
public:
    /**
     * @brief Create the loader main frame.
     * @param[in] registry_hive_path The UTF-8 DOS path of the sandbox registry
     *                               hive file which the browser shows.
     */
    explicit MainFrame(const std::string& registry_hive_path);
    virtual ~MainFrame();

    struct Data;
private:
    Data* data_;
};

#endif // APPBOX_LOADER_WIDGET_MAIN_FRAME_HPP
