#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/GetPEB.hpp"
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
    { "NtQueryInformationByName",            appbox::InjectNtQueryInformationByName            },
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
    sys.OSBuild = appbox::GetPEB().ImageBuild;

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
