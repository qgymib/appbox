#ifndef APPBOX_LOADER_REGISTRY_NODE_HPP
#define APPBOX_LOADER_REGISTRY_NODE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <memory>
#include <string>
#include <vector>

namespace appbox
{

struct RegistryNode
{
    typedef std::shared_ptr<RegistryNode> Ptr;

    enum class Type
    {
        /**
         * @see https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
         * @{
         */
        TYPE_BINARY = REG_BINARY,
        TYPE_DWROD = REG_DWORD,
        TYPE_DWORD_BIG_ENDIAN = REG_DWORD_BIG_ENDIAN,
        TYPE_EXPAND_SZ = REG_EXPAND_SZ,
        TYPE_LINK = REG_LINK,
        TYPE_MULTI_SZ = REG_MULTI_SZ,
        TYPE_NONE = REG_NONE,
        TYPE_QWORD = REG_QWORD,
        TYPE_SZ = REG_SZ,
        /**
         * @}
         */

        /* Extended types. */
        TYPE_PATH, /* This is a path node. */
    };

    Type                 type; /* Node type */
    std::wstring         name; /* The name of the node. */
    std::vector<uint8_t> data; /* The data of the node. */
};

} // namespace appbox

#endif
