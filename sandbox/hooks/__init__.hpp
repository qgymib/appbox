#ifndef APPBOX_HOOKS_INIT_HPP
#define APPBOX_HOOKS_INIT_HPP

#include <initializer_list>

namespace appbox
{

/**
 * @brief Hook function information block.
 */
struct Detour
{
    const char*                           name;   /* The function name. */
    std::initializer_list<const wchar_t*> search; /* DLL list to search the function. */
    void*                                 hook;   /* Hook function. */
    void*                                 orig;   /* Original function. */
};

extern Detour CreateProcessInternalW; /* KernelBase.dll */
extern Detour NtCreateFile;           /* ntdll.dll */

/**
 * @brief Initialize hook.
 */
void InitHook();

} // namespace appbox

#endif
