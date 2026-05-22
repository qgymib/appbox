#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "hook/CreateProcessInternalW.hpp"
#include "hook/LdrQueryImageFileExecutionOptionsEx.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryDirectoryFileEx.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/NtQueryInformationFile.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtReadFile.hpp"
#include "hook/NtSetInformationFile.hpp"
#include "hook/NtWriteFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "hook/SetProcessMitigationPolicy.hpp"
#include "__init__.hpp"
#include <exception>
#include <detours.h>

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

static const SandboxHook s_hooks[] = {
    { "CreateProcessInternalW",              appbox::InjectCreateProcessInternalW              },
    { "LdrQueryImageFileExecutionOptionsEx", appbox::InjectLdrQueryImageFileExecutionOptionsEx },
    { "NtClose",                             appbox::InjectNtClose                             },
    { "NtCreateFile",                        appbox::InjectNtCreateFile                        },
    { "NtDeleteFile",                        appbox::InjectNtDeleteFile                        },
    { "NtOpenFile",                          appbox::InjectNtOpenFile                          },
    { "NtQueryAttributesFile",               appbox::InjectNtQueryAttributesFile               },
    { "NtQueryDirectoryFile",                appbox::InjectNtQueryDirectoryFile                },
    { "NtQueryDirectoryFileEx",              appbox::InjectNtQueryDirectoryFileEx              },
    { "NtQueryFullAttributesFile",           appbox::InjectNtQueryFullAttributesFile           },
    { "NtQueryInformationFile",              appbox::InjectNtQueryInformationFile              },
    { "NtQueryObject",                       appbox::InjectNtQueryObject                       },
    { "NtReadFile",                          appbox::InjectNtReadFile                          },
    { "NtSetInformationFile",                appbox::InjectNtSetInformationFile                },
    { "NtWriteFile",                         appbox::InjectNtWriteFile                         },
    { "RtlInitUnicodeString",                appbox::InjectRtlInitUnicodeString                },
    { "SetProcessMitigationPolicy",          appbox::InjectSetProcessMitigationPolicy          },
};

appbox::Sys appbox::sys;

void appbox::InitHook()
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    sys.OSBuild = GET_PEB_IMAGE_BUILD;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    sys.h_ntdll = GetModuleHandleW(L"ntdll.dll");
    sys.h_kernel32 = GetModuleHandleW(L"kernel32.dll");
    sys.h_kernelbase = GetModuleHandleW(L"kernelbase.dll");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (const auto& hook : s_hooks)
    {
        hook.fn();
    }

    DetourTransactionCommit();
}
