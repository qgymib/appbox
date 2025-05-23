#include <Windows.h>
#include <detours.h>
#include "utils/macros.hpp"
#include "__init__.hpp"

static appbox::hook::Func* s_hooks[] = {
    &appbox::hook::CreateFileA,
    &appbox::hook::CreateFileW,
    &appbox::hook::LoadLibraryA,
    &appbox::hook::LoadLibraryW,
};

void appbox::hook::Init()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (size_t i = 0; i < ARRAY_SIZE(s_hooks); i++)
    {
        appbox::hook::Func* f = s_hooks[i];
        if (f->OrigAddr != nullptr)
        {
            DetourAttach(&f->OrigAddr, f->HookAddr);
        }
    }

    DetourTransactionCommit();
}
