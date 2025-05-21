#include <wx/wx.h>
#include <wx/cmdline.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "utils/file.hpp"
#include "utils/pair.hpp"
#include "widgets/MainFrame.hpp"
#include "App.hpp"

wxIMPLEMENT_APP(PackerApp);

static const appbox::Pair<const wchar_t*, spdlog::level::level_enum> s_spdlog_levels[] = {
    { L"trace",    spdlog::level::trace    },
    { L"debug",    spdlog::level::debug    },
    { L"info",     spdlog::level::info     },
    { L"warn",     spdlog::level::warn     },
    { L"error",    spdlog::level::err      },
    { L"critical", spdlog::level::critical },
    { L"off",      spdlog::level::off      },
};

bool PackerApp::OnInit()
{
    nogui = false;

    /* The command line arguments parser here. */
    if (!wxApp::OnInit())
    {
        return false;
    }
    spdlog::info("Welcome to use AppBox Packer");

    MainFrame* f = new MainFrame;
    f->Show(!nogui);

    return true;
}

void PackerApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    /* clang-format off */
    static const wxCmdLineEntryDesc options[] = {
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "loader",
            "Loader path",
            wxCMD_LINE_VAL_STRING,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "template",
            "Template file path",
            wxCMD_LINE_VAL_STRING,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "process",
            "Process directly",
            wxCMD_LINE_VAL_NONE,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "nogui",
            "Do not show gui",
            wxCMD_LINE_VAL_NONE,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "loglevel",
            "[trace|debug|info|warn|error|critical|off]",
            wxCMD_LINE_VAL_STRING,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        wxCMD_LINE_DESC_END,
    };
    /* clang-format on */

    parser.SetDesc(options);
    parser.SetSwitchChars("-");

    wxApp::OnInitCmdLine(parser);
}

bool PackerApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    nogui = parser.Found("nogui");
    process = parser.Found("process");

    wxString value;
    if (parser.Found("loader", &value))
    {
        loaderPath = value;
    }
    else
    {
        std::wstring path = appbox::DirName(appbox::GetExePath());
        loaderPath = path + L"\\loader.exe";
    }
    if (parser.Found("template", &value))
    {
        templatePath = value;
    }
    if (parser.Found("loglevel", &value))
    {
        spdlog::level::level_enum log_level = appbox::PairSearchK(
            s_spdlog_levels, std::size(s_spdlog_levels), value.c_str().AsWChar());
        spdlog::set_level(log_level);
    }

    if (!templatePath.empty())
    {
        std::string data = appbox::ReadFileAll(templatePath.c_str());
        templateJson = nlohmann::json::parse(data);
    }

    return wxApp::OnCmdLineParsed(parser);
}
