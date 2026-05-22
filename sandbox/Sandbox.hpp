#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "utils/WinAPI.h"
#include "utils/Task.hpp"
#include "utils/AsyncInstance.hpp"
#include "utils/PipeClient.hpp"
#include "Config.hpp"

namespace appbox
{

struct Sandbox
{
    Sandbox(HINSTANCE hinstDLL);

    HINSTANCE                           hinstDLL;    /* Dll instance */
    appbox::SandboxConfig               inject_data; /* Inject data */
    TaskQueue::Ptr                      task_queue;  /* Task queue */
    std::shared_ptr<appbox::PipeClient> client;      /* RPC client */
};

/**
 * @brief Global sandbox instance
 */
extern Sandbox* sandbox;

} // namespace appbox

#endif
