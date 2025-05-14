#include <Windows.h>
#include <detours.h>
#include "__init__.hpp"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

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
