#ifndef APPBOX_COMMON_EMBEDDED_RESOURCE_HPP
#define APPBOX_COMMON_EMBEDDED_RESOURCE_HPP

#include <windows.h>
#include <string>
#include <string_view>

namespace appbox
{

/**
 * @brief Read a payload which an executable carries as a resource.
 *
 * The packer carries the launcher program as an RCDATA resource of its own
 * executable, and the launcher carries the two sandbox injection modules and its
 * run configuration the same way. A pack run and a run of an extracted archive
 * therefore need no payload beside the executable.
 *
 * The identifiers are declared by the executable which carries the payload:
 * `src/core/EmbeddedResourceIds.h` holds the identifier of the packer and
 * `launcher/EmbeddedResourceIds.h` holds those of the launcher. Every executable
 * numbers its resources on its own, so a resource id is only meaningful for the
 * module which declares it and the same number can name different payloads in
 * different executables.
 *
 * The returned view points into the image of the module which carries the
 * resource, so reading a payload copies nothing and the view stays valid for as
 * long as that module is loaded: the payload of the running executable is read
 * once and handed to the packer as it is.
 *
 * @param[in] module Module which carries the resource; a null handle reads the
 *                   resources of the running executable.
 * @param[in] resource_id Identifier of the payload in that module.
 * @param[out] payload The bytes of the payload, empty when the call fails.
 * @param[out] error Description of the failure, empty when the call succeeds.
 * @return true when the payload was read, false otherwise.
 */
inline bool ReadEmbeddedResource(void* module, int resource_id, std::string_view& payload, std::string& error)
{
    payload = {};
    error.clear();

    const auto instance = static_cast<HMODULE>(module);
    const auto name = MAKEINTRESOURCEW(static_cast<WORD>(resource_id));

    /*
     * RT_RCDATA is spelled through MAKEINTRESOURCE, which follows the character
     * set of the translation unit: this project does not define UNICODE, so the
     * macro hands an ANSI string to the wide lookup even though the type of a
     * resource is an integer id. The cast keeps the symbolic name of the type
     * without changing what is looked up.
     */
    const auto type = reinterpret_cast<LPCWSTR>(RT_RCDATA);

    const auto resource = FindResourceW(instance, name, type);
    if (resource == nullptr)
    {
        error = "the embedded resource " + std::to_string(resource_id) + " is missing";
        return false;
    }

    /*
     * The size is read before the resource is locked, so an empty payload is
     * reported as such instead of being handed to a caller which cannot tell it
     * apart from a resource which was not found.
     */
    const auto size = SizeofResource(instance, resource);
    if (size == 0)
    {
        error = "the embedded resource " + std::to_string(resource_id) + " is empty";
        return false;
    }

    const auto data = static_cast<const char*>(LockResource(LoadResource(instance, resource)));
    if (data == nullptr)
    {
        error = "the embedded resource " + std::to_string(resource_id) + " cannot be locked";
        return false;
    }

    payload = std::string_view(data, size);
    return true;
}

} // namespace appbox

#endif // APPBOX_COMMON_EMBEDDED_RESOURCE_HPP
