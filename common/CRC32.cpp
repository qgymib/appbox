#include "WinAPI.hpp"
#include "CRC32.hpp"

static uint32_t crc32_table[256];

static BOOL CALLBACK InitCRC32Table(PINIT_ONCE, PVOID, PVOID*)
{
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (size_t j = 0; j < 8; j++)
        {
            if (c & 1)
            {
                c = polynomial ^ (c >> 1);
            }
            else
            {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }

    return TRUE;
}

uint32_t appbox::CRC32::Update(uint32_t initial, const void* data, size_t size)
{
    static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&s_once, InitCRC32Table, nullptr, nullptr);

    uint32_t c = initial ^ 0xFFFFFFFF;
    auto     u = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        c = crc32_table[(c ^ u[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFF;
}
