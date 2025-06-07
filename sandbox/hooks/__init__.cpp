#include <Windows.h>
#include <detours.h>
#include <spdlog/spdlog.h>
#include "utils/macros.hpp"
#include "utils/pair.hpp"
#include "__init__.hpp"
#include "appbox.hpp"

static appbox::Detour* s_hooks[] = {
    &appbox::CreateProcessInternalW,
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

    for (size_t i = 0; i < std::size(s_hooks); i++)
    {
        appbox::Detour* f = s_hooks[i];

        HMODULE hModule = appbox::PairSearchK(dlls, std::size(dlls), f->dll, wcscmp);
        if ((f->orig = GetProcAddress(hModule, f->name)) == nullptr)
        {
            continue;
        }

        /* Inject function */
        LONG status = DetourAttach(&f->orig, f->hook);
        if (status)
        {
            std::string s = fmt::format("DetourAttach(%s): %lu", f->name, status);
            throw std::runtime_error(s);
        }
    }

    DetourTransactionCommit();
}
