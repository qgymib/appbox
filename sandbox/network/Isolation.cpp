#include "utils/WinAPI.h" /* Must be first include file */
#include <fstream>
#include <iterator>
#include <string>
#include "utils/Log.hpp"
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

    const std::wstring& path = appbox::sandbox->wNetworkIsolationDOSPath;
    if (path.empty())
    {
        LOG_D("no network isolation file is configured");
        return STATUS_SUCCESS;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        LOG_D("the network isolation file does not exist: {}", appbox::WideToUTF8(path));
        return STATUS_SUCCESS;
    }

    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    std::string error;
    if (!appbox::sandbox->dns_table.Parse(text, error))
    {
        LOG_W("the network isolation file is ignored: {}", error);
        return STATUS_SUCCESS;
    }

    LOG_I("network isolation loaded: {} DNS redirections", appbox::sandbox->dns_table.Count());
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
}
