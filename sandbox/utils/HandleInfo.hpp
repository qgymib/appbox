#ifndef APPBOX_SANDBOX_UTILS_HANDLE_INFO_HPP
#define APPBOX_SANDBOX_UTILS_HANDLE_INFO_HPP

#include "utils/WinAPI.h"
#include "filesystem/Resolve.hpp"
#include <memory>
#include <string>
#include <functional>

namespace appbox
{

struct HandleInfo
{
    typedef std::shared_ptr<HandleInfo> Ptr;
    typedef std::function<void(Ptr)>    Fn;

    static NTSTATUS Init();
    static void     Exit();

    /**
     * @brief Create handle information.
     * @param[in] handle Handle.
     * @param[in] fn Create callback.
     * @return Information pointer.
     */
    static Ptr Create(HANDLE handle, Fn fn);

    /**
     * @brief Search for handle.
     * @param[in] handle Handle.
     * @return Handle information.
     */
    static Ptr Find(HANDLE handle);

    /**
     * @brief Remove handle information.
     * @param[in] handle Handle to remove.
     * @return Information.
     */
    static Ptr Pop(HANDLE handle);

    /**
     * @brief Raw handle
     */
    HANDLE handle = nullptr;

    /**
     * @brief NT path in sandbox view.
     */
    std::wstring viewPath;

    /**
     * @brief Resolve information.
     */
    appbox::filesystem::ResolveResult resolve;

    /**
     * @brief File attributes
     * @see OBJ_CASE_INSENSITIVE
     * @see OBJ_INHERIT
     */
    ULONG Attributes = 0;
};

} // namespace appbox

#endif
