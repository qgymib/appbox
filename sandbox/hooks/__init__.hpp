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
};

extern const Detour CreateProcessInternalW; /* Kernel32 */
extern const Detour NtCreateFile;           /* ntdll.dll */

/**
 * @brief Initialize hook.
 */
void InitHook();

} // namespace appbox

#endif
