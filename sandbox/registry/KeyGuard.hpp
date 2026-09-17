#ifndef APPBOX_SANDBOX_REGISTRY_KEYGUARD_HPP
#define APPBOX_SANDBOX_REGISTRY_KEYGUARD_HPP

#include "hook/NtClose.hpp"

namespace appbox
{
namespace registry
{

/**
 * @brief RAII owner of a registry key handle which was opened by a hook.
 *
 * The guard closes the handle through the unhooked NtClose entry point, so a
 * hook never re-enters a detour while unwinding. A null handle is ignored.
 */
class KeyGuard
{
public:
    /**
     * @brief Take the ownership of the handle.
     * @param[in] handle The handle to close on destruction, may be null.
     */
    explicit KeyGuard(HANDLE handle = nullptr) : handle_(handle)
    {
    }

    ~KeyGuard()
    {
        if (handle_ != nullptr && sys_NtClose != nullptr)
        {
            sys_NtClose(handle_);
        }
    }

    KeyGuard(const KeyGuard&) = delete;
    KeyGuard& operator=(const KeyGuard&) = delete;
    KeyGuard(KeyGuard&&) = delete;
    KeyGuard& operator=(KeyGuard&&) = delete;

    /**
     * @brief The owned handle.
     */
    HANDLE get() const
    {
        return handle_;
    }

private:
    HANDLE handle_;
};

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_KEYGUARD_HPP
