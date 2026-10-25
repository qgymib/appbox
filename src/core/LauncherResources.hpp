#ifndef APPBOX_PACKER_CORE_LAUNCHER_RESOURCES_HPP
#define APPBOX_PACKER_CORE_LAUNCHER_RESOURCES_HPP

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
 * @brief Embed the launcher resources into a launcher payload.
 *
 * Two payloads are written as RCDATA resources of the launcher payload, using
 * the identifiers of `launcher/EmbeddedResourceIds.h`, which are the resources
 * the launcher reads at run time (see `launcher/main.cpp` and
 * `launcher/Splash.cpp`):
 *
 * - `IDR_APPBOX_LAUNCHER_CONFIG` always carries the UTF-8 JSON document of the
 *   run configuration.
 * - `IDR_APPBOX_STARTUP_IMAGE` carries the bytes of the startup image, and only
 *   when `startup_image` is not empty. The payload the packer starts from is the
 *   launcher as it was built, which never carries an image resource, so a
 *   session without an image needs no update at all instead of a deletion.
 *
 * Both resources are written in one resource update session, so the payload is
 * copied to a temporary file once, because the resource update API of Windows
 * works on files, and the patched image is read back.
 *
 * A payload which cannot carry the configuration is an error: the configuration
 * is what the launcher starts, so a pack run which cannot embed it produces an
 * archive which cannot run.
 *
 * @param[in] launcher_bytes Embedded launcher payload.
 * @param[in] launcher_size Payload size in bytes.
 * @param[in] config_json UTF-8 JSON document of the configuration.
 * @param[in] startup_image Bytes of the startup image, empty while the session
 *                          sets no image.
 * @param[out] error Description of the failure, empty when the call succeeds.
 * @return The patched image, empty on failure.
 */
std::vector<char> EmbedLauncherResources(const void* launcher_bytes, std::size_t launcher_size,
                                         std::string_view config_json, std::string_view startup_image,
                                         std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_LAUNCHER_RESOURCES_HPP
