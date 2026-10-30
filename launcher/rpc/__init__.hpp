#ifndef APPBOX_LAUNCHER_RPC_INIT_HPP
#define APPBOX_LAUNCHER_RPC_INIT_HPP

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include "RemoteSession.hpp"

/**
 * @brief Registered RPC methods
 * The method name must match to message name.
 */
/* clang-format off */
#define APPBOX_LAUNCHER_RPC_METHODS(xx)   \
    xx(MsgEnvironment)                    \
    xx(MsgLauncherStart)                  \
    xx(MsgRunState)
/* clang-format on */

/**
 * @brief Register an RPC method on one session.
 *
 * The methods of the launcher are registered per session and not per pipe,
 * because the answer of a request belongs to the session which received it: a
 * session is the only place which knows the request ids of its peer. The
 * implementation therefore receives the session it serves as its first
 * parameter.
 *
 * @param[in] NAME Message name.
 * @param[in] ID Request ID name.
 * @param[in] PARAM Parameter name.
 */
#define APPBOX_LAUNCHER_RPC_DEFINE(NAME, ID, PARAM)                                                                    \
    static void RpcMethod_##NAME(const ::appbox::RemoteSession::Ptr& session, uint64_t ID,                             \
                                 const ::appbox::NAME::Req& PARAM);                                                    \
    static void RpcMethod_##NAME##_Wrap(const ::appbox::RemoteSession::Ptr& session, uint64_t id,                      \
                                        const nlohmann::json& param)                                                   \
    {                                                                                                                  \
        RpcMethod_##NAME(session, id, param);                                                                          \
    }                                                                                                                  \
    void ::appbox::RegisterRpcMethod_##NAME(const ::appbox::RemoteSession::Ptr& session)                               \
    {                                                                                                                  \
        session->RegisterMethod(::appbox::NAME::Method,                                                                \
                                ::appbox::BindSessionMethod(session, &RpcMethod_##NAME##_Wrap));                       \
    }                                                                                                                  \
    static void RpcMethod_##NAME(const ::appbox::RemoteSession::Ptr& session, uint64_t ID,                             \
                                 const ::appbox::NAME::Req& PARAM)

namespace appbox
{

/**
 * @brief Bind a method implementation to the session it is registered on.
 *
 * The handler of a session is stored inside the session, so it holds the session
 * weakly: a strong reference would make the session own itself, and a connection
 * which ended would never be released.
 *
 * @tparam[in] Fn Kind of the implementation, which takes the session, the
 *                request id and the parameter of the request.
 * @param[in] session Session the method is registered on.
 * @param[in] fn Implementation of the method.
 * @return The handler to register.
 */
template <typename Fn>
RemoteSession::MethodHandler BindSessionMethod(const RemoteSession::Ptr& session, Fn fn)
{
    std::weak_ptr<RemoteSession> weak_session = session;

    return [weak_session, fn](std::uint64_t id, const nlohmann::json& param) {
        /* A session is alive while it dispatches, so the lock never fails here. */
        if (auto bound = weak_session.lock())
        {
            fn(bound, id, param);
        }
    };
}

#define APPBOX_LAUNCHER_RPC_METHODS_EXPAND(NAME) void RegisterRpcMethod_##NAME(const ::appbox::RemoteSession::Ptr&);
APPBOX_LAUNCHER_RPC_METHODS(APPBOX_LAUNCHER_RPC_METHODS_EXPAND)
#undef APPBOX_LAUNCHER_RPC_METHODS_EXPAND

/**
 * @brief Initialize RPC methods.
 * @param[in] session Session to register the methods on.
 */
void RpcInit(const appbox::RemoteSession::Ptr& session);

} // namespace appbox

#endif // APPBOX_LAUNCHER_RPC_INIT_HPP
