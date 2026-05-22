#ifndef APPBOX_UTILS_BITPARSER_HPP
#define APPBOX_UTILS_BITPARSER_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

struct BitData
{
    const char* name; /* Field name */
    uint64_t    mask; /* Bit mask */
};

/**
 * @brief Parse bit field. If \p bits is 0 and the last record of \p data has mask 0, set record to the name of last
 * record.
 * @param[in] bits Bit field
 * @param[in] data Bit data
 * @param[in] nmemb Number of bit data
 * @return JSON object
 */
nlohmann::json ParseBit(uint64_t bits, const BitData* data, size_t nmemb);

} // namespace appbox

#endif
