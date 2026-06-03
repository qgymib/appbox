#include <wx/wx.h>
#include <detours.h>
#include "utils/GetExecutableDir.hpp"
#include "ProcessJob.hpp"
#include "Loader.hpp"
#include "BuildCommandLine.hpp"
#include "Defines.hpp"

struct appbox::ProcessJob::Data
{
    Data();
    ~Data();

    HANDLE                    hIOCP;            /* IO Completion Port. */
    HANDLE                    hJob;             /* Job object. */
    PROCESS_INFORMATION       process_info;     /* Process information. */
    std::string               sandbox_dll_path; /* Inject sandbox dll path. */
    std::wstring              exe_path;         /* Target executable path to run. */
    std::vector<std::wstring> exe_args;         /* Target executable command line. */
    DWORD                     exit_code;        /* Exit code of target executable. */
};

appbox::ProcessJob::Data::Data()
{
    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    hJob = CreateJobObjectW(nullptr, nullptr);
    ZeroMemory(&process_info, sizeof(process_info));
    exit_code = 0;

#if defined(_WIN64)
    sandbox_dll_path = wxGetApp().runtime->inject_data.sandbox64_dos_path;
#else
    sandbox_dll_path = wxGetApp().runtime->inject_data.sandbox32_dos_path;
#endif

    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp;
        ZeroMemory(&acp, sizeof(acp));
        acp.CompletionKey = hJob;
        acp.CompletionPort = hIOCP;
        SetInformationJobObject(hJob, JobObjectAssociateCompletionPortInformation, &acp, sizeof(acp));
    }
}

appbox::ProcessJob::Data::~Data()
{
    if (process_info.hThread != nullptr)
    {
        CloseHandle(process_info.hThread);
        process_info.hThread = nullptr;
    }
    if (process_info.hProcess != nullptr)
    {
        CloseHandle(process_info.hProcess);
        process_info.hProcess = nullptr;
    }

    CloseHandle(hIOCP);
    CloseHandle(hJob);
}

appbox::ProcessJob::ProcessJob(const std::wstring exePath, const std::vector<std::wstring> args)
{
    data_ = new Data;
    data_->exe_path = exePath;
    data_->exe_args = args;
}

appbox::ProcessJob::~ProcessJob()
{
    delete data_;
}

DWORD appbox::ProcessJob::GetExitCode()
{
    return data_->exit_code;
}

DWORD appbox::ProcessJob::Start()
{
    auto                      self_path = appbox::GetExecutablePath();
    std::vector<std::wstring> self_args = { L"--X-AppBox-Launcher=true", data_->exe_path };
    self_args.insert(self_args.end(), data_->exe_args.begin(), data_->exe_args.end());
    auto cmdline = appbox::BuildCommandLine(self_path, self_args);

    STARTUPINFOW startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    if (!DetourCreateProcessWithDllExW(self_path.c_str(), cmdline.data(), nullptr, nullptr, false, CREATE_SUSPENDED,
                                       nullptr, nullptr, &startupInfo, &data_->process_info,
                                       data_->sandbox_dll_path.c_str(), nullptr))
    {
        return GetLastError();
    }

    const GUID  guid = SANDBOX_GUID;
    std::string inject_data = nlohmann::json(wxGetApp().runtime->inject_data).dump();
    DWORD       inject_data_sz = static_cast<DWORD>(inject_data.size());
    if (!DetourCopyPayloadToProcess(data_->process_info.hProcess, guid, inject_data.c_str(), inject_data_sz))
    {
        data_->exit_code = GetLastError();
        TerminateProcess(data_->process_info.hProcess, data_->exit_code);
        return data_->exit_code;
    }

    if (!AssignProcessToJobObject(data_->hJob, data_->process_info.hProcess))
    {
        data_->exit_code = GetLastError();
        TerminateProcess(data_->process_info.hProcess, data_->exit_code);
        return data_->exit_code;
    }

    ResumeThread(data_->process_info.hThread);
    CloseHandle(data_->process_info.hThread);
    data_->process_info.hThread = nullptr;

    return 0;
}

DWORD appbox::ProcessJob::Wait(DWORD dwMilliseconds)
{
    for (;;)
    {
        DWORD        code = 0;
        ULONG_PTR    key = 0;
        LPOVERLAPPED ov = nullptr;
        if (!GetQueuedCompletionStatus(data_->hIOCP, &code, &key, &ov, dwMilliseconds))
        {
            return GetLastError();
        }

        if (key == (ULONG_PTR)data_->hJob && code == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO)
        { /* All processes in the job have exited. */
            break;
        }
    }

    if (!GetExitCodeProcess(data_->process_info.hProcess, &data_->exit_code))
    {
        return GetLastError();
    }

    return 0;
}
