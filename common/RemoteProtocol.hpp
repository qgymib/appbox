#ifndef APPBOX_COMMON_REMOTE_PROTOCOL_HPP
#define APPBOX_COMMON_REMOTE_PROTOCOL_HPP

#include <cstdint>

#define APPBOX_REMOTE_MAGIC 0x424F583A

namespace appbox
{

/**
 * @brief Header of one RPC frame exchanged over the named pipe transport.
 *
 * The structure is shared by the loader side and the sandbox side of the RPC
 * link, so it belongs to the common module and must stay layout compatible in
 * both the 32 bit and the 64 bit build.
 */
struct RemoteProtocol
{
    uint32_t magic = APPBOX_REMOTE_MAGIC;
    uint32_t length = 0;
};

} // namespace appbox

#endif // APPBOX_COMMON_REMOTE_PROTOCOL_HPP
