#ifndef APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_HPP
#define APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_HPP

#include <string>
#include <string_view>

namespace appbox
{

/**
 * @brief Read a payload which an executable carries as a resource.
 *
 * The packer carries the loader program and the two sandbox injection modules
 * as RCDATA resources of its own executable (see
 * `src/core/EmbeddedResourceIds.h` for the identifiers and
 * `cmake/EmbeddedResources.rc.in` for the declarations), so a pack run needs no
 * file beside the executable.
 *
 * The returned view points into the image of the module which carries the
 * resource, so reading a payload copies nothing and the view stays valid for as
 * long as that module is loaded: the payload of the running executable is read
 * once and handed to the packer as it is.
 *
 * @param[in] module Module which carries the resource; a null handle reads the
 *                   resources of the running executable.
 * @param[in] resource_id Identifier of the payload, see
 *                        `src/core/EmbeddedResourceIds.h`.
 * @param[out] payload The bytes of the payload, empty when the call fails.
 * @param[out] error Description of the failure, empty when the call succeeds.
 * @return true when the payload was read, false otherwise.
 */
bool ReadEmbeddedResource(void* module, int resource_id, std::string_view& payload, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_HPP
