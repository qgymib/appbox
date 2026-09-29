#include "EmbeddedResource.hpp"
#include <windows.h>
#include <string>

namespace appbox
{

bool ReadEmbeddedResource(void* module, int resource_id, std::string_view& payload, std::string& error)
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
