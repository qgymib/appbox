#ifndef APPBOX_PACKER_WIDGETS_STARTUP_FILES_DIALOG_HPP
#define APPBOX_PACKER_WIDGETS_STARTUP_FILES_DIALOG_HPP

#include <wx/wx.h>
#include <nlohmann/json.hpp>
#include <vector>

class StartupFilesDialog : public wxDialog
{
public:
    struct File
    {
        std::string path;    /* File path in sandbox. */
        std::string args;    /* Command line arguments. */
        std::string trigger; /* Trigger keyword. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(File, path, args, trigger);
    };
    struct Config
    {
        std::vector<File> files; /* Startup files. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Config, files);
    };

public:
    StartupFilesDialog(wxWindow* parent, const Config& config);
    ~StartupFilesDialog() override;

    Config GetResult() const;

private:
    struct Data;
    Data* mData;
};

#endif
