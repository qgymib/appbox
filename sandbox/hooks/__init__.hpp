#ifndef APPBOX_HOOKS_INIT_HPP
#define APPBOX_HOOKS_INIT_HPP

namespace appbox
{

/**
 * @brief Hook function information block.
 */
struct Detour
{
    const char*    name; /* The function name. */
    const wchar_t* dll;  /* DLL to hook. */
    void*          hook; /* Hook function. */
    void*          orig; /* Original function. */
};

extern Detour CreateProcessInternalW; /* DLL:Kernel32 */

/**
 * @brief Initialize hook.
 */
void InitHook();

} // namespace appbox

#endif
