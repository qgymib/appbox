#include <Windows.h>
#include <detours.h>
#include "utils/macros.hpp"
#include "__init__.hpp"

static const appbox::Detour* s_hooks[] = {
    &appbox::CreateProcessInternalW,
    &appbox::NtCreateFile,
};

void appbox::InitHook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (size_t i = 0; i < ARRAY_SIZE(s_hooks); i++)
    {
        const appbox::Detour* f = s_hooks[i];
        f->init();
    }

    DetourTransactionCommit();
}
