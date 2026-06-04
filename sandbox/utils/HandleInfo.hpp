#ifndef APPBOX_SANDBOX_UTILS_HANDLE_INFO_HPP
#define APPBOX_SANDBOX_UTILS_HANDLE_INFO_HPP

#include "utils/WinAPI.h"
#include "filesystem/Resolve.hpp"
#include <memory>
#include <string>
#include <functional>

namespace appbox
{

class HandleInfo
{
public:
    typedef std::shared_ptr<HandleInfo> Ptr;
    typedef std::function<void(Ptr)>    Fn;

    struct Meta
    {
        typedef std::shared_ptr<Meta>    Ptr;
        typedef std::function<Ptr(void)> CreateFn;

        virtual ~Meta() = default; /* Make it virtual */
    };
    struct Data;

    /**
     * @brief Initialize handle information.
     * @return Error code.
     */
    static NTSTATUS Init();

    /**
     * @brief Exit handle information.
     */
    static void Exit();

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
     * @brief Find meta. If not found, create it with \p fn and insert with \p key.
     * @param[in] key Key.
     * @param[in] fn Create callback.
     * @return Meta data.
     */
    Meta::Ptr MetaFindOr(uint64_t key, Meta::CreateFn fn);

    /**
     * @brief Remove meta.
     * @param[in] key Meta key.
     */
    void MetaDrop(uint64_t key);

    /**
     * @brief Raw handle
     */
    HANDLE handle;

    /**
     * @brief NT path in sandbox view.
     */
    std::wstring viewPath;

    /**
     * @brief Resolve information.
     */
    appbox::filesystem::ResolveResult::Ptr resolve;

    /**
     * @brief File attributes
     * @see OBJ_CASE_INSENSITIVE
     * @see OBJ_INHERIT
     */
    ULONG ObjAttributes;

    ~HandleInfo();
    HandleInfo(const HandleInfo&) = delete;
    HandleInfo(HandleInfo&&) = delete;
    HandleInfo& operator=(const HandleInfo&) = delete;
    HandleInfo& operator=(HandleInfo&&) = delete;

private:
    HandleInfo();
    Data* data_;
};

} // namespace appbox

#endif
