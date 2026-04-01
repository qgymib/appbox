#ifndef APPBOX_COMMON_CRC32_HPP
#define APPBOX_COMMON_CRC32_HPP

#include <cstdint>

namespace appbox
{

struct CRC32
{
    static uint32_t Update(uint32_t initial, const void* data, size_t size);
};

}

#endif
