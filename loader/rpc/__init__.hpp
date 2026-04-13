#ifndef APPBOX_LOADER_RPC_INIT_HPP
#define APPBOX_LOADER_RPC_INIT_HPP

#include <nlohmann/json.hpp>
#include "RemoteServer.hpp"

/**
 * @brief Registered RPC methods
 * The method name must match to message name.
 */
/* clang-format off */
#define APPBOX_LOADER_RPC_METHODS(xx)   \
    xx(MsgLog)
/* clang-format on */

/**
 * @brief Register an RPC method.
 * @param[in] NAME Message name.
 * @param[in] ID Request ID name.
 * @param[in] PARAM Parameter name.
 */
#define APPBOX_LOADER_RPC_DEFINE(NAME, ID, PARAM)                                                  \
    static void RpcMethod_##NAME(uint64_t ID, const ::appbox::NAME::Req& PARAM);                   \
    static void RpcMethod_##NAME##_Wrap(uint64_t id, const nlohmann::json& param)                  \
    {                                                                                              \
        RpcMethod_##NAME(id, param);                                                               \
    }                                                                                              \
    void ::appbox::RegisterRpcMethod_##NAME(::appbox::RemoteServer::Ptr s)                         \
    {                                                                                              \
        s->RegisterMethod(::appbox::NAME::method, RpcMethod_##NAME##_Wrap);                        \
    }                                                                                              \
    static void RpcMethod_##NAME(uint64_t ID, const ::appbox::NAME::Req& PARAM)

namespace appbox
{

#define APPBOX_LOADER_RPC_METHODS_EXPAND(NAME)                                                     \
    void RegisterRpcMethod_##NAME(::appbox::RemoteServer::Ptr);
APPBOX_LOADER_RPC_METHODS(APPBOX_LOADER_RPC_METHODS_EXPAND)
#undef APPBOX_LOADER_RPC_METHODS_EXPAND

/**
 * @brief Initialize RPC methods.
 */
void RpcInit(RemoteServer::Ptr s);

} // namespace appbox

#endif // APPBOX_LOADER_RPC_INIT_HPP
