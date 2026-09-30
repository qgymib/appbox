#ifndef APPBOX_SANDBOX_UTILS_GET_PEB_HPP
#define APPBOX_SANDBOX_UTILS_GET_PEB_HPP

#include "utils/WinAPI.h"
#include <string>

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

/**
 * @brief Path of the executable of this process.
 *
 * The path is read from the process environment block, which the module reads
 * itself instead of asking the operating system: the sandbox calls this while
 * it is attached, before its hooks are installed, so it must not depend on an
 * entry point it resolved itself.
 *
 * @return The path of the image of the process, empty when the block does not
 *         hold one.
 */
std::wstring GetImagePathFromPeb();

} // namespace appbox

#endif
