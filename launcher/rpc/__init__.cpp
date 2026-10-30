#include "Launcher.hpp"
#include "__init__.hpp"

struct RpcMethodRecord
{
    void (*fn)(const appbox::RemoteSession::Ptr&);
};

#define EXPAND_AS_METHOD(NAME) { appbox::RegisterRpcMethod_##NAME },
static const RpcMethodRecord s_methods[] = { APPBOX_LAUNCHER_RPC_METHODS(EXPAND_AS_METHOD) };
#undef EXPAND_AS_METHOD

void appbox::RpcInit(const appbox::RemoteSession::Ptr& session)
{
    for (auto m : s_methods)
    {
        m.fn(session);
    }
}
