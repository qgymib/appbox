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

    if (appbox::sandbox->wFilesystemIsolationDOSPaths.empty())
    {
        LOG_D("no filesystem isolation file is configured");
        return STATUS_SUCCESS;
    }

    /*
     * The isolation file lists paths of the virtual filesystem, whose first
     * component is the layer key of a layer. The key of a layer is the name of
     * its folder inside the base filesystem, which the launcher mapped into the
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

    /*
     * The files are read in the order of the layers of the run: the file of
     * the resources of the archive first, then the file of every patch package
     * in ascending order. Every file overrides the modes the files below it
     * set for the same path, so the sandboxed process observes the mode of the
     * last layer which names a path.
     *
     * A file which cannot be used is logged and skipped, like a malformed
     * isolation file of the archive is: a broken patch package must not fail
     * the run.
     */
    std::size_t unmapped_count = 0;
    for (const auto& path : appbox::sandbox->wFilesystemIsolationDOSPaths)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            LOG_D("the filesystem isolation file does not exist: {}", appbox::WideToUTF8(path));
            continue;
        }

        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        std::vector<std::wstring> unmapped;
        std::string               error;
        if (!appbox::sandbox->fs_isolation.Parse(text, layers, unmapped, error))
        {
            LOG_W("the filesystem isolation file '{}' is ignored: {}", appbox::WideToUTF8(path), error);
            continue;
        }

        for (const auto& entry : unmapped)
        {
            LOG_W("the filesystem isolation file '{}' lists a path which no layer of the view maps: {}",
                  appbox::WideToUTF8(path), appbox::WideToUTF8(entry));
        }
        unmapped_count += unmapped.size();
    }

    LOG_I("filesystem isolation modes loaded: {} entries, {} paths without a layer",
          appbox::sandbox->fs_isolation.Count(), unmapped_count);
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
