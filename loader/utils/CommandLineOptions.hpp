#ifndef APPBOX_LOADER_UTILS_COMMANDLINEOPTIONS_HPP
#define APPBOX_LOADER_UTILS_COMMANDLINEOPTIONS_HPP

#include "sandbox/utils/WinAPI.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox
{

struct CommandLineOptions
{
    CommandLineOptions();
    ~CommandLineOptions();

    /**
     * @brief Parse command line options
     * @return True if continue, false if exit.
     */
    bool ParseOptions();

    int                      wargc;           /* Command line argument count */
    LPWSTR*                  wargv;           /* Command line argument array */
    bool                     is_launcher;     /* True if it is a mini launcher */
    std::wstring             config_dir;      /* Config file directory path */
    nlohmann::json           override_config; /* Override config */
    std::vector<std::string> extra_args;      /* Extra arguments, encoding in UTF-8 */
};

} // namespace appbox

#endif
