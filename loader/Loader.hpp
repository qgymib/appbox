#ifndef APPBOX_LOADER_HPP
#define APPBOX_LOADER_HPP

#include <filesystem>
#include <memory>
#include "InjectData.hpp"
#include "RemoteServer.hpp"

namespace appbox
{

struct Loader
{
    typedef std::shared_ptr<Loader> Ptr;
    ~Loader();

    static Ptr Create(const std::wstring& sandbox_path);

    std::filesystem::path temp_dir;    /* Temporary directory for loader operations */
    InjectData            inject_data; /* Inject data information */
    RemoteServer::Ptr     pipe_server; /* Named pipe server. */
};

/**
 * @brief Global loader context.
 */
extern Loader::Ptr loader;

} // namespace appbox

#endif // APPBOX_LOADER_HPP
