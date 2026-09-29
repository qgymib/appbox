#include "utils/Winsock.hpp" /* Must be first include file */
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include "utils/Log.hpp"
#include "network/Proxy.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "Isolation.hpp"

NTSTATUS appbox::network::Isolation::Init()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        /* Nothing to do outside isolation mode; the hooks are not attached. */
        return STATUS_SUCCESS;
    }

    /*
     * The configuration of the run is the configuration of its layers: the
     * file of the resources of the archive comes first and the file of every
     * patch package follows in the order the packages take effect in. The
     * redirections of every file are merged into the table and the proxy of a
     * layer overrides the proxy of the layers below it, so the file of the
     * last layer which names a usable proxy decides how the traffic of the
     * application is carried.
     *
     * The module is initialized before the hooks are attached, so the files
     * are read through the original entry points of the process.
     */
    ProxyConfig config;
    bool        configured = false;

    for (const auto& path : appbox::sandbox->wNetworkIsolationDOSPaths)
    {
        if (path.empty())
        {
            continue;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            LOG_D("the network isolation file does not exist: {}", appbox::WideToUTF8(path));
            continue;
        }

        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        std::string error;
        if (!appbox::sandbox->dns_table.Parse(text, error))
        {
            /*
             * The document is not a network isolation file, so neither its
             * redirections nor its proxy take part in the run: the layers
             * below it stay in place.
             */
            LOG_W("the network isolation file '{}' is ignored: {}", appbox::WideToUTF8(path), error);
            continue;
        }

        /*
         * A layer which carries no proxy or whose proxy cannot be used keeps
         * the proxy of the layers below it, so the configuration which is
         * applied at the end is the one of the last layer which names a proxy.
         */
        ProxyConfig layer;
        if (ParseProxyConfig(text, layer) && layer.IsEnabled())
        {
            config = layer;
            configured = true;
        }
    }

    LOG_I("network isolation loaded: {} DNS redirections", appbox::sandbox->dns_table.Count());

    if (!configured)
    {
        LOG_D("no layer configures a proxy");
        return STATUS_SUCCESS;
    }

    appbox::sandbox->proxy = std::make_shared<Proxy>();
    appbox::sandbox->proxy->Configure(config);

    return STATUS_SUCCESS;
}

void appbox::network::Isolation::Exit()
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    /* The table belongs to the sandbox instance, which owns it for the run. */
    appbox::sandbox->dns_table.Clear();

    if (appbox::sandbox->proxy != nullptr)
    {
        appbox::sandbox->proxy->Reset();
        appbox::sandbox->proxy.reset();
    }
}
