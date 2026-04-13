#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include <memory>
#include <string>
#include "WinAPI.hpp"
#include "utils/Task.hpp"
#include "utils/AsyncInstance.hpp"
#include "RemoteClient.hpp"

namespace appbox
{

struct Sandbox
{
    typedef std::shared_ptr<Sandbox> Ptr;

    /**
     * @brief Create a new sandbox context.
     * @param[in] hinstDLL Dll instance
     * @return Sandbox context.
     */
    static Ptr Create(HINSTANCE hinstDLL);

    HINSTANCE    hinstDLL;               /* Dll instance */
    std::string  pipe_path;              /* Named pipe path */
    std::wstring sandbox_path_dos;       /* Absolute path of sandbox directory */
    std::wstring sandbox_path_nt;        /* Absolute path of sandbox in NT format */
    std::string  sandbox32_dll_path_dos; /* Absolute path of 32-bit sandbox dll, encoding in UTF-8 */
    std::string  sandbox64_dll_path_dos; /* Absolute path of 64-bit sandbox dll, encoding in UTF-8 */

    TaskQueue::Ptr                   task_queue; /* Task queue */
    AsyncInstance<RemoteClient>::Ptr client;     /* RPC client */
};

/**
 * @brief Global sandbox instance
 */
extern Sandbox::Ptr sandbox;

} // namespace appbox

#endif
