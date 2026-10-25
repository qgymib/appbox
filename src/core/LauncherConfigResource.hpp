#ifndef APPBOX_PACKER_CORE_LAUNCHER_CONFIG_RESOURCE_HPP
#define APPBOX_PACKER_CORE_LAUNCHER_CONFIG_RESOURCE_HPP

#include "Config.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief Serialize a launcher configuration as UTF-8 JSON.
 *
 * The document is indented and written without a byte order mark. Non-ASCII
 * characters are kept as their UTF-8 bytes instead of being escaped, so the
 * text of the resource is the text of the configuration: a trigger or an
 * argument which the user typed in a language other than English stays
 * readable in the resource.
 *
 * @param[in] config Configuration of the run.
 * @return The JSON document.
 */
std::string BuildLauncherConfigJson(const LauncherConfig& config);

/**
 * @brief Embed the launcher configuration into a launcher payload.
 *
 * The configuration is written as the RCDATA resource
 * `IDR_APPBOX_LAUNCHER_CONFIG` of `launcher/EmbeddedResourceIds.h`, which is the
 * resource the launcher reads at run time (see `launcher/main.cpp`). The image
 * is copied into a temporary file, because the resource update API of Windows
 * works on files, and the patched image is read back.
 *
 * A payload which cannot carry the configuration is an error: the configuration
 * is what the launcher starts, so a pack run which cannot embed it produces an
 * archive which cannot run.
 *
 * @param[in] launcher_bytes Embedded launcher payload.
 * @param[in] launcher_size Payload size in bytes.
 * @param[in] json UTF-8 JSON document of the configuration.
 * @param[out] error Description of the failure, empty when the call succeeds.
 * @return The patched image, empty on failure.
 */
std::vector<char> EmbedLauncherConfig(const void* launcher_bytes, std::size_t launcher_size, std::string_view json,
                                      std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_LAUNCHER_CONFIG_RESOURCE_HPP
