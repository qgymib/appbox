#ifndef APPBOX_PACKER_WIDGET_LOADER_RESOURCE_HPP
#define APPBOX_PACKER_WIDGET_LOADER_RESOURCE_HPP

#include <string>
#include <string_view>

namespace appbox
{

/**
 * @brief Get the embedded AppBoxLoader.exe payload.
 *
 * The payload is embedded at build time through CMakeRC, so the returned
 * view stays valid for the process lifetime.
 *
 * @param[out] error Error description on failure.
 * @return Payload bytes, empty on failure.
 */
std::string_view LoadEmbeddedLoader(std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_WIDGET_LOADER_RESOURCE_HPP
