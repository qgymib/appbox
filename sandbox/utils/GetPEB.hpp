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
 * @brief Address of the environment pointer of the process parameters.
 *
 * The counted writer of the environment of `ntdll` takes the address of the
 * block it writes, and the address of the environment pointer of the process
 * parameters is the way a caller names the environment of the process itself
 * instead of passing a null block. The sandbox compares the argument of the
 * entry point against this address, so a write which names the environment of
 * the process is observed like a write with a null block.
 *
 * @return The address of the pointer, null when the block of the process does
 *         not carry process parameters.
 */
PWSTR* GetProcessEnvironmentSlot();

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

/**
 * @brief Working directory of this process.
 *
 * The directory is read from the process environment block for the same reason
 * the image path is: the value is the one the process reports, and the read
 * needs no entry point of the system.
 *
 * @return The working directory of the process, empty when the block does not
 *         hold one.
 */
std::wstring GetCurrentDirectoryFromPeb();

} // namespace appbox

#endif
