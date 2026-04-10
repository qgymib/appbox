#ifndef APPBOX_LOADER_HPP
#define APPBOX_LOADER_HPP

#include <filesystem>
#include "InjectData.hpp"
#include "RemoteServer.hpp"

namespace appbox
{

struct Loader
{
    Loader();
    ~Loader();

    std::wstring              exe_path;      /* Main executable path */
    std::vector<std::wstring> exe_args; /* Main executable arguments */

    std::filesystem::path temp_dir;    /* Temporary directory for loader operations */
    InjectData            inject_data; /* Inject data information */
    RemoteServer::Ptr     pipe_server; /* Named pipe server. */
};

/**
 * @brief Global loader context.
 */
extern Loader* loader;

void InitLoader();
void ExitLoader();

} // namespace appbox

#endif // APPBOX_LOADER_HPP
