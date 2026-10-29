#ifndef APPBOX_PACKER_CORE_SANDBOX_CONFIG_PAYLOAD_HPP
#define APPBOX_PACKER_CORE_SANDBOX_CONFIG_PAYLOAD_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Write the configuration of a launcher into a launcher payload.
 *
 * The injection module of the sandbox isolates every process it is injected
 * into, a launcher included: a packaged application which starts the launcher of
 * another archive therefore runs that launcher inside its own sandbox, which is
 * what the configuration removes. The launcher and the sandbox write the
 * configuration of a process under the GUID of `APPBOX_SANDBOX_GUID`
 * (`sandbox/utils/Defines.hpp`), and the member `hook_enabled` of
 * `sandbox/Config.hpp` decides whether the module works at all: the packer
 * writes the configuration into the image of every launcher it produces with the
 * member turned off, and the module skips a process whose configuration turns
 * the hooks off (see `sandbox/Sandbox.cpp`).
 *
 * The payload is written with the binary API of Detours, which stores it in a
 * section of its own (`.detour`) instead of in a resource: the section is mapped
 * while the image runs, so the module finds the payload in the process without
 * opening a file, and the resource directory of the image stays what the
 * launcher reads its configuration from. The call runs after the resources of
 * the run were embedded, so a later resource update cannot drop the section.
 *
 * The binary API works on files, so the image is copied to a temporary file, the
 * patched image is written to a second one and read back. A file which a filter
 * driver of the machine holds for a moment is retried like the resource update
 * of `src/core/PeResourcePatch.hpp` does it.
 *
 * @param[in] image_bytes Payload of the launcher, as the packer embedded it.
 * @param[in] image_size Payload size in bytes.
 * @param[out] error Description of the failure, empty when the call succeeds.
 * @return The patched image, empty on failure.
 */
std::vector<char> EmbedSandboxConfigPayload(const void* image_bytes, std::size_t image_size, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SANDBOX_CONFIG_PAYLOAD_HPP
