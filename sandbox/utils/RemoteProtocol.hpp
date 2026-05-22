#ifndef APPBOX_UTILS_REMOTE_PROTOCOL_HPP
#define APPBOX_UTILS_REMOTE_PROTOCOL_HPP

#include <cstdint>

#define APPBOX_REMOTE_MAGIC 0x424F583A

namespace appbox
{

struct RemoteProtocol
{
    uint32_t magic = APPBOX_REMOTE_MAGIC;
    uint32_t length = 0;
};

} // namespace appbox

#endif
