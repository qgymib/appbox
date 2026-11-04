#ifndef APPBOX_COMMON_EMBEDDED_RESOURCE_HPP
#define APPBOX_COMMON_EMBEDDED_RESOURCE_HPP

#include "ErrorCode.hpp"
#include <tl/expected.hpp>
#include <string_view>
#include <windows.h>

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
 * @return The bytes of the payload, or the code of the failure:
 *         `kEmbeddedResourceMissing` when the module carries no resource of that
 *         identifier, `kEmbeddedResourceEmpty` when the resource holds no byte,
 *         and `kEmbeddedResourceLockFailed` when the bytes cannot be handed out.
 */
inline tl::expected<std::string_view, ErrorCode> ReadEmbeddedResource(void* module, int resource_id) noexcept
{
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
        return tl::unexpected<ErrorCode>(ErrorCode::kEmbeddedResourceMissing);
    }

    /*
     * The size is read before the resource is locked, so an empty payload is
     * reported as such instead of being handed to a caller which cannot tell it
     * apart from a resource which was not found.
     */
    const auto size = SizeofResource(instance, resource);
    if (size == 0)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kEmbeddedResourceEmpty);
    }

    const auto data = static_cast<const char*>(LockResource(LoadResource(instance, resource)));
    if (data == nullptr)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kEmbeddedResourceLockFailed);
    }

    return std::string_view(data, size);
}

} // namespace appbox

#endif // APPBOX_COMMON_EMBEDDED_RESOURCE_HPP
