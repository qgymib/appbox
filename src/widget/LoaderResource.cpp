#include "LoaderResource.hpp"
#include <cmrc/cmrc.hpp>

CMRC_DECLARE(appbox_resource);

namespace appbox
{

std::string_view LoadEmbeddedLoader(std::string& error)
{
    try
    {
        auto          filesystem = cmrc::appbox_resource::get_filesystem();
        auto          file = filesystem.open("AppBoxLoader.exe");
        std::string_view payload(file.begin(), file.size());
        if (payload.empty())
        {
            error = "the embedded loader payload is empty";
            return {};
        }
        return payload;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to load the embedded loader: ") + e.what();
        return {};
    }
}

} // namespace appbox
