#ifndef APPBOX_HOOKS_INIT_HPP
#define APPBOX_HOOKS_INIT_HPP

namespace appbox
{

/**
 * @brief Hook function information block.
 */
struct Detour
{
    const wchar_t* name; /* The function name. */
    void (*init)();      /* Detour initialize point. */
#if 0
    void* OrigAddr;      /* The function address. */
    void* HookAddr;      /* The hook function address. */
#endif
};

extern Detour CreateProcessInternalW; /* DLL:Kernel32 */

/**
 * @brief Initialize hook.
 */
void InitHook();

} // namespace appbox

#endif
