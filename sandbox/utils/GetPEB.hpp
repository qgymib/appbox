#ifndef APPBOX_SANDBOX_UTILS_GET_PEB_HPP
#define APPBOX_SANDBOX_UTILS_GET_PEB_HPP

#include "utils/WinAPI.h"

namespace appbox
{

struct PEB
{
    DWORD ImageBuild;
};

/**
 * @brief Get Process Environment Block.
 * @return PEB
 */
PEB GetPEB();

} // namespace appbox

#endif
