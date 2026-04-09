#ifndef APPBOX_LOADER_HPP
#define APPBOX_LOADER_HPP

#include "InjectData.hpp"
#include "RemoteServer.hpp"

namespace appbox
{

struct Loader
{
    std::string       executable_path; /* Main executable path */
    InjectData        inject_data;     /* Inject data information */
    RemoteServer::Ptr pipe_server;     /* Named pipe server. */
};

/**
 * @brief Global loader context.
 */
extern Loader* loader;

void InitLoader();
void ExitLoader();

} // namespace appbox

#endif // APPBOX_LOADER_HPP
