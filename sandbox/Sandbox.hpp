#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "WinAPI.hpp"
#include "utils/Task.hpp"
#include "utils/AsyncInstance.hpp"
#include "InjectData.hpp"
#include "RemoteClient.hpp"

namespace appbox
{

struct Sandbox
{
    Sandbox(HINSTANCE hinstDLL);

    HINSTANCE                        hinstDLL;    /* Dll instance */
    appbox::InjectData               inject_data; /* Inject data */
    TaskQueue::Ptr                   task_queue;  /* Task queue */
    AsyncInstance<RemoteClient>::Ptr client;      /* RPC client */
};

/**
 * @brief Global sandbox instance
 */
extern Sandbox* sandbox;

} // namespace appbox

#endif
