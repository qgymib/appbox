#include <Windows.h>
#include <detours.h>
#include "utils/macros.hpp"
#include "__init__.hpp"

static appbox::Detour* s_hooks[] = {
    &appbox::CreateProcessInternalW,
};

void appbox::InitHook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (size_t i = 0; i < ARRAY_SIZE(s_hooks); i++)
    {
        appbox::Detour* f = s_hooks[i];
        f->init();
    }

    DetourTransactionCommit();
}
