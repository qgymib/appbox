#include "BitParser.hpp"

nlohmann::json appbox::ParseBit(uint64_t bits, const BitData* data, size_t nmemb)
{
    nlohmann::json arr;
    if (bits == 0 && data[nmemb - 1].mask == 0)
    {
        arr.push_back(data[nmemb - 1].name);
        return arr;
    }

    for (size_t i = 0; i < nmemb; i++)
    {
        if (data[i].mask != 0 && (bits & data[i].mask) == data[i].mask)
        {
            arr.push_back(data[i].name);
            bits &= ~data[i].mask;
        }
    }
    if (bits != 0)
    {
        arr.push_back(bits);
    }
    return arr;
}
