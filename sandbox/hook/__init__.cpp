#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <exception>
#include <spdlog/spdlog.h>
#include <detours.h>
#include "__init__.hpp"

#ifdef _M_ARM64
#define NtCurrentPeb() (*((ULONG_PTR*)(__getReg(18) + 0x60)))
#elif _WIN64
#define NtCurrentPeb() ((ULONG_PTR)__readgsqword(0x60))
#else // _M_X86
#define NtCurrentPeb() ((ULONG_PTR)__readfsdword(0x30))
#endif

// Pointer to 64-bit PEB_LDR_DATA is at offset 0x0018 of 64-bit PEB
// Pointer to 32-bit PEB_LDR_DATA is at offset 0x000C of 32-bit PEB
#define GET_ADDR_OF_PEB NtCurrentPeb()

#ifdef _WIN64

#define GET_PEB_LDR_DATA (*(PEB_LDR_DATA**)(GET_ADDR_OF_PEB + 0x18))
#define GET_PEB_IMAGE_BASE (*(ULONG_PTR*)(GET_ADDR_OF_PEB + 0x10))
#define GET_PEB_MAJOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0x118))
#define GET_PEB_MINOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0x11c))
#define GET_PEB_IMAGE_BUILD (*(USHORT*)(GET_ADDR_OF_PEB + 0x120))

#else

#define GET_PEB_LDR_DATA (*(PEB_LDR_DATA**)(GET_ADDR_OF_PEB + 0x0C))
#define GET_PEB_IMAGE_BASE (*(ULONG_PTR*)(GET_ADDR_OF_PEB + 0x08))
#define GET_PEB_MAJOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0xa4))
#define GET_PEB_MINOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0xa8))
#define GET_PEB_IMAGE_BUILD (*(USHORT*)(GET_ADDR_OF_PEB + 0xac))

#endif

struct SandboxHook
{
    const char* name; /* function name */
    void (*fn)();     /* Register function */
};

#define EXPAND_HOOK_AS_FUNC(NAME) { #NAME, &appbox::Register_##NAME },
static const SandboxHook s_hooks[] = { APPBOX_SANDBOX_HOOKS(EXPAND_HOOK_AS_FUNC) };
#undef EXPAND_HOOK_AS_FUNC

appbox::Sys appbox::sys;

void appbox::InitHook()
{
    sys.OSBuild = GET_PEB_IMAGE_BUILD;
    sys.h_ntdll = GetModuleHandleW(L"ntdll.dll");
    sys.h_kernel32 = GetModuleHandleW(L"kernel32.dll");
    sys.h_kernelbase = GetModuleHandleW(L"kernelbase.dll");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (const auto& hook : s_hooks)
    {
        try
        {
            hook.fn();
        }
        catch (const std::exception& e)
        {
            spdlog::error("Sandbox hook {} failed: {}", hook.name, e.what());
        }
    }

    DetourTransactionCommit();
}
