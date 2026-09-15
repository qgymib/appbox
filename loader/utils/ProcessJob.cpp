#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <spdlog/spdlog.h>
#include "sandbox/utils/Defines.hpp"
#include "utils/GetExecutableDir.hpp"
#include "ProcessJob.hpp"
#include "BuildCommandLine.hpp"

struct appbox::ProcessJob::Data
{
    Data(const appbox::SandboxConfig& cfg);
    ~Data();

    HANDLE                    hIOCP;            /* IO Completion Port. */
    HANDLE                    hJob;             /* Job object. */
    PROCESS_INFORMATION       process_info;     /* Process information. */
    appbox::SandboxConfig     inject_data;      /* Injection data. */
    std::wstring              exe_path;         /* Target executable path to run. */
    std::vector<std::wstring> exe_args;         /* Target executable command line. */
    DWORD                     exit_code;        /* Exit code of target executable. */
};

appbox::ProcessJob::Data::Data(const appbox::SandboxConfig& cfg)
    : hIOCP(nullptr), hJob(nullptr), inject_data(cfg), exit_code(0)
{
    ZeroMemory(&process_info, sizeof(process_info));

    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (hIOCP == nullptr)
    {
        SPDLOG_ERROR("failed to create the IO completion port: {}", GetLastError());
        return;
    }

    hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob == nullptr)
    {
        SPDLOG_ERROR("failed to create the job object: {}", GetLastError());
        return;
    }

    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
        {
            SPDLOG_ERROR("failed to set the job object limit: {}", GetLastError());
        }
    }

    {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp;
        ZeroMemory(&acp, sizeof(acp));
        acp.CompletionKey = hJob;
        acp.CompletionPort = hIOCP;
        if (!SetInformationJobObject(hJob, JobObjectAssociateCompletionPortInformation, &acp, sizeof(acp)))
        {
            SPDLOG_ERROR("failed to associate the job object with the completion port: {}", GetLastError());
        }
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

    if (hIOCP != nullptr)
    {
        CloseHandle(hIOCP);
        hIOCP = nullptr;
    }
    if (hJob != nullptr)
    {
        CloseHandle(hJob);
        hJob = nullptr;
    }
}

appbox::ProcessJob::ProcessJob(const std::wstring exePath, const std::vector<std::wstring> args,
                               const appbox::SandboxConfig& inject_data)
{
    data_ = new Data(inject_data);
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
    if (data_->hIOCP == nullptr || data_->hJob == nullptr)
    {
        SPDLOG_ERROR("the job object was not initialized");
        return ERROR_INVALID_HANDLE;
    }

#if defined(_WIN64)
    const std::string& sandbox_dll_path = data_->inject_data.sandbox64_dos_path;
#else
    const std::string& sandbox_dll_path = data_->inject_data.sandbox32_dos_path;
#endif

    auto                      self_path = appbox::GetExecutablePath();
    std::vector<std::wstring> self_args = { L"--X-AppBox-Launcher=true", data_->exe_path };
    self_args.insert(self_args.end(), data_->exe_args.begin(), data_->exe_args.end());
    auto cmdline = appbox::BuildCommandLine(self_path, self_args);

    STARTUPINFOW startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    if (!DetourCreateProcessWithDllExW(self_path.c_str(), cmdline.data(), nullptr, nullptr, false, CREATE_SUSPENDED,
                                       nullptr, nullptr, &startupInfo, &data_->process_info,
                                       sandbox_dll_path.c_str(), nullptr))
    {
        return GetLastError();
    }

    const GUID  guid = APPBOX_SANDBOX_GUID;
    std::string inject_data = nlohmann::json(data_->inject_data).dump();
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

    if (ResumeThread(data_->process_info.hThread) == static_cast<DWORD>(-1))
    {
        const DWORD err = GetLastError();
        SPDLOG_ERROR("failed to resume the target process: {}", err);
        data_->exit_code = err;
        TerminateProcess(data_->process_info.hProcess, err);
        return err;
    }

    CloseHandle(data_->process_info.hThread);
    data_->process_info.hThread = nullptr;

    return 0;
}

DWORD appbox::ProcessJob::Wait(DWORD dwMilliseconds)
{
    if (data_->hIOCP == nullptr)
    {
        return ERROR_INVALID_HANDLE;
    }

    for (;;)
    {
        DWORD        code = 0;
        ULONG_PTR    key = 0;
        LPOVERLAPPED ov = nullptr;
        if (!GetQueuedCompletionStatus(data_->hIOCP, &code, &key, &ov, dwMilliseconds))
        {
            const DWORD err = GetLastError();

            /*
             * A wait timeout is reported as WAIT_TIMEOUT, the caller expects
             * ERROR_TIMEOUT. A failing operation must never be reported as a
             * successful wait.
             */
            if (ov == nullptr && err == WAIT_TIMEOUT)
            {
                return ERROR_TIMEOUT;
            }

            return (err != ERROR_SUCCESS) ? err : ERROR_TIMEOUT;
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
