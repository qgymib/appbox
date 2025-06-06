#include <Windows.h>
#include <detours.h>
#include "utils/macros.hpp"
#include "utils/pair.hpp"
#include "__init__.hpp"
#include "appbox.hpp"
#include "spdlog/fmt/bundled/color.h"

static appbox::Detour* s_hooks[] = {
    &appbox::CreateProcessInternalW,
    &appbox::NtCreateFile,
    &appbox::NtQueryObject,
};

void appbox::InitHook()
{
    const appbox::Pair<const wchar_t*, HMODULE> dlls[] = {
        { L"ntdll.dll",      appbox::G->hNtdll      },
        { L"kernel32.dll",   appbox::G->hKernel32   },
        { L"KernelBase.dll", appbox::G->hKernelBase },
    };

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (size_t i = 0; i < ARRAY_SIZE(s_hooks); i++)
    {
        appbox::Detour* f = s_hooks[i];

        /* Search for functions in search dll list. */
        for (auto it = f->search.begin(); it != f->search.end(); ++it)
        {
            HMODULE hModule = appbox::PairSearchK(dlls, ARRAY_SIZE(dlls), *it, wcscmp);
            if ((f->orig = GetProcAddress(hModule, f->name)) != nullptr)
            {
                break;
            }
        }

        /* Inject function */
        if (f->orig != nullptr)
        {
            LONG status = DetourAttach(&f->orig, f->hook);
            if (status)
            {
                std::string s = fmt::format("DetourAttach(%s): %lu", f->name, status);
                throw std::runtime_error(s);
            }
        }
    }

    DetourTransactionCommit();
}
