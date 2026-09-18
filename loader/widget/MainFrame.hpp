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
    /**
     * @brief Install the icon of the loader as the icon of the frame.
     *
     * The icon is taken from the resource of the loader executable, so the
     * window and the taskbar button always show the icon of the loader even
     * when the executable carries the icon of a packaged application as well.
     */
    void ApplyWindowIcon();

    Data* data_;
};

#endif // APPBOX_LOADER_WIDGET_MAIN_FRAME_HPP
