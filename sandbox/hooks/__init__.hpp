#ifndef APPBOX_HOOKS_INIT_HPP
#define APPBOX_HOOKS_INIT_HPP

namespace appbox
{

namespace hook
{

/**
 * @brief Hook function information block.
 */
struct Func
{
    const wchar_t* dll;      /* The library where function imported. */
    const wchar_t* name;     /* The function name. */
    void*          OrigAddr; /* The function address. */
    void*          HookAddr; /* The hook function address. */
};

extern Func CreateFileA;  /* DLL:Kernel32 */
extern Func CreateFileW;  /* DLL:Kernel32 */
extern Func LoadLibraryA; /* DLL:Kernel32 */
extern Func LoadLibraryW; /* DLL:Kernel32 */

/**
 * @brief Initialize hook.
 */
void Init();

} // namespace Hook

} // namespace appbox

#endif
