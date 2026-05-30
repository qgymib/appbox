#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/GetPEB.hpp"
#include "hook/CreateProcessInternalW.hpp"
#include "hook/LdrQueryImageFileExecutionOptionsEx.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtDeleteFile.hpp"
#include "hook/NtDeviceIoControlFile.hpp"
#include "hook/NtFsControlFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryDirectoryFileEx.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/NtQueryInformationByName.hpp"
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

struct SandboxHook
{
    const char* name;    /* function name */
    void (*fn_attach)(); /* Attach function */
    void (*fn_detach)(); /* Detach function. */
};

/* clang-format off */
static const SandboxHook s_hooks[] = {
    { "CreateProcessInternalW",                 appbox::AttachCreateProcessInternalW,               appbox::DetachCreateProcessInternalW              },
    { "LdrQueryImageFileExecutionOptionsEx",    appbox::AttachLdrQueryImageFileExecutionOptionsEx,  appbox::DetachLdrQueryImageFileExecutionOptionsEx },
    { "NtClose",                                appbox::AttachNtClose,                              appbox::DetachNtClose                             },
    { "NtCreateFile",                           appbox::AttachNtCreateFile,                         appbox::DetachNtCreateFile                        },
    { "NtDeleteFile",                           appbox::AttachNtDeleteFile,                         appbox::DetachNtDeleteFile                        },
    { "NtDeviceIoControlFile",                  appbox::AttachNtDeviceIoControlFile,                appbox::DetachNtDeviceIoControlFile               },
    { "NtFsControlFile",                        appbox::AttachNtFsControlFile,                      appbox::DetachNtFsControlFile                     },
    { "NtOpenFile",                             appbox::AttachNtOpenFile,                           appbox::DetachNtOpenFile                          },
    { "NtQueryAttributesFile",                  appbox::AttachNtQueryAttributesFile,                appbox::DetachNtQueryAttributesFile               },
    { "NtQueryDirectoryFile",                   appbox::AttachNtQueryDirectoryFile,                 appbox::DetachNtQueryDirectoryFile                },
    { "NtQueryDirectoryFileEx",                 appbox::AttachNtQueryDirectoryFileEx,               appbox::DetachNtQueryDirectoryFileEx              },
    { "NtQueryFullAttributesFile",              appbox::AttachNtQueryFullAttributesFile,            appbox::DetachNtQueryFullAttributesFile           },
    { "NtQueryInformationByName",               appbox::AttachNtQueryInformationByName,             appbox::DetachNtQueryInformationByName            },
    { "NtQueryInformationFile",                 appbox::AttachNtQueryInformationFile,               appbox::DetachNtQueryInformationFile              },
    { "NtQueryObject",                          appbox::AttachNtQueryObject,                        appbox::DetachNtQueryObject                       },
    { "NtReadFile",                             appbox::AttachNtReadFile,                           appbox::DetachNtReadFile                          },
    { "NtSetInformationFile",                   appbox::AttachNtSetInformationFile,                 appbox::DetachNtSetInformationFile                },
    { "NtWriteFile",                            appbox::AttachNtWriteFile,                          appbox::DetachNtWriteFile                         },
    { "RtlInitUnicodeString",                   appbox::AttachRtlInitUnicodeString,                 appbox::DetachRtlInitUnicodeString                },
    { "SetProcessMitigationPolicy",             appbox::AttachSetProcessMitigationPolicy,           appbox::DetachSetProcessMitigationPolicy          },
};
/* clang-format on */

appbox::Sys appbox::sys;

NTSTATUS appbox::InitHook()
{
    sys.OSBuild = appbox::GetPEB().ImageBuild;

    sys.h_ntdll = GetModuleHandleW(L"ntdll.dll");
    sys.h_kernel32 = GetModuleHandleW(L"kernel32.dll");
    sys.h_kernelbase = GetModuleHandleW(L"kernelbase.dll");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (const auto& hook : s_hooks)
    {
        hook.fn_attach();
    }

    DetourTransactionCommit();

    return 0;
}

void appbox::ExitHook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (const auto& hook : s_hooks)
    {
        hook.fn_detach();
    }

    DetourTransactionCommit();
}
