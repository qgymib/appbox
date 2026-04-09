#include "Loader.hpp"
#include "__init__.hpp"

struct RpcMethodRecord
{
    void (*fn)(appbox::RemoteServer::Ptr);
};

#define EXPAND_AS_METHOD(NAME) { appbox::RegisterRpcMethod_##NAME },
static const RpcMethodRecord s_methods[] = {
    APPBOX_LOADER_RPC_METHODS(EXPAND_AS_METHOD)
};
#undef EXPAND_AS_METHOD

void appbox::RpcInit()
{
    for (auto m : s_methods)
    {
        m.fn(appbox::loader->pipe_server);
    }
}
