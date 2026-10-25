#include "core/PeResourcePatch.hpp"
#include "LauncherConfigReader.hpp"
#include "EmbeddedResource.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include <exception>
#include <string>
#include <string_view>

bool appbox::test::ReadLauncherConfigJson(const std::wstring& image_path, std::string& json, std::string& error)
{
    json.clear();
    error.clear();

    const auto module = appbox::pe_resource::LoadAsDataFile(image_path, error);
    if (module == nullptr)
    {
        return false;
    }
    const appbox::pe_resource::LoadedImage image(module);

    std::string_view payload;
    if (!appbox::ReadEmbeddedResource(module, IDR_APPBOX_LAUNCHER_CONFIG, payload, error))
    {
        return false;
    }

    json.assign(payload.data(), payload.size());
    return true;
}

bool appbox::test::ReadLauncherConfig(const std::wstring& image_path, appbox::LauncherConfig& config,
                                      std::string& error)
{
    std::string json;
    if (!ReadLauncherConfigJson(image_path, json, error))
    {
        return false;
    }

    try
    {
        config = nlohmann::json::parse(json).get<appbox::LauncherConfig>();
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to parse the launcher configuration: ") + e.what();
        return false;
    }
}
