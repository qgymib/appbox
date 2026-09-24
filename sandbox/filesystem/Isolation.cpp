#include "utils/WinAPI.h" /* Must be first include file */
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "utils/Log.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "Isolation.hpp"

NTSTATUS appbox::filesystem::Isolation::Init()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        /* Nothing to do outside isolation mode; the hooks are not attached. */
        return STATUS_SUCCESS;
    }

    const std::wstring& path = appbox::sandbox->wFilesystemIsolationDOSPath;
    if (path.empty())
    {
        LOG_D("no filesystem isolation file is configured");
        return STATUS_SUCCESS;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        LOG_D("the filesystem isolation file does not exist: {}", appbox::WideToUTF8(path));
        return STATUS_SUCCESS;
    }

    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    /*
     * The isolation file lists paths of the virtual filesystem, whose first
     * component is the layer key of a layer. The key of a layer is the name of
     * its folder inside the base filesystem, which the loader mapped into the
     * view, so the table can translate the listed paths into view paths.
     */
    std::vector<IsolationLayer> layers;
    for (const auto& mapping : appbox::sandbox->fs.fs_lower)
    {
        IsolationLayer layer;
        layer.layer_key = IsolationTable::LayerKeyOf(mapping.host_nt_path);
        layer.mapped_nt_path = mapping.mapped_nt_path;
        if (layer.layer_key.empty())
        {
            continue;
        }
        layers.push_back(std::move(layer));
    }

    std::vector<std::wstring> unmapped;
    std::string               error;
    if (!appbox::sandbox->fs_isolation.Parse(text, layers, unmapped, error))
    {
        LOG_W("the filesystem isolation file is ignored: {}", error);
        return STATUS_SUCCESS;
    }

    for (const auto& entry : unmapped)
    {
        LOG_W("the filesystem isolation file lists a path which no layer of the view maps: {}",
              appbox::WideToUTF8(entry));
    }

    LOG_I("filesystem isolation modes loaded: {} entries, {} paths without a layer",
          appbox::sandbox->fs_isolation.Count(), unmapped.size());
    return STATUS_SUCCESS;
}

void appbox::filesystem::Isolation::Exit()
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    /* The table belongs to the sandbox instance, which owns it for the run. */
    appbox::sandbox->fs_isolation = IsolationTable();
}
