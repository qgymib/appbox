#include "WinAPI.h" /* Must be first include file */
#include "ResolveCore.hpp"
#include "Sandbox.hpp"
#include "LayerPath.hpp"

bool appbox::filesystem::RebaseLayerPathToView(const std::wstring& layerPath, std::wstring& viewPath)
{
    if (appbox::sandbox == nullptr)
    {
        return false;
    }

    const auto view_path = RebaseLayerPathToViewCore(appbox::sandbox->fs, layerPath);
    if (!view_path)
    {
        return false;
    }

    viewPath = *view_path;
    return true;
}
