#include "utils/WinAPI.h" /* Must be first include file */
#include "ResolveCore.hpp"
#include "Sandbox.hpp"
#include "LayerPath.hpp"

bool appbox::filesystem::RebaseLayerPathToView(const std::wstring& layerPath, std::wstring& viewPath)
{
    if (appbox::sandbox == nullptr)
    {
        return false;
    }

    return RebaseLayerPathToViewCore(appbox::sandbox->fs, layerPath, viewPath);
}
