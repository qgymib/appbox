#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include <memory>
#include <string>
#include "WinAPI.hpp"
#include "utils/Task.hpp"
#include "utils/AsyncInstance.hpp"
#include "InjectData.hpp"
#include "RemoteClient.hpp"

namespace appbox
{

struct Sandbox
{
    typedef std::shared_ptr<Sandbox> Ptr;

    static Ptr Create(HINSTANCE hinstDLL);

    HINSTANCE    hinstDLL;  /* Dll instance */
    std::string  pipe_path; /* Named pipe path */
    std::wstring sandbox_path;
    std::string  sandbox32_dll_path;
    std::string  sandbox64_dll_path;

    TaskQueue::Ptr                   task_queue; /* Task queue */
    AsyncInstance<RemoteClient>::Ptr client;     /* RPC client */
};

/**
 * @brief Global sandbox instance
 */
extern Sandbox::Ptr sandbox;

} // namespace appbox

#endif
