#include "WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <spdlog/spdlog.h>
#include <exception>
#include <vector>
#include "sandbox/utils/Defines.hpp"
#include "PeImage.hpp"
#include "WString.hpp"
#include "ProcessJob.hpp"
#include "BuildCommandLine.hpp"

/*
 * The SDK declares the job attribute of a creation below `_WIN32_WINNT_WIN7`,
 * while this module asks for the API set of Vista. The value is the one
 * `ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)` computes: the attribute is
 * an input attribute of the process which is created.
 */
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
#define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

namespace
{

/**
 * @brief Report whether the image of a process is a 32 bit image.
 *
 * The module which is injected into a process has to match its bit size, and
 * the process does not exist yet when the launcher picks the module: the bit
 * size is read from the image itself. An image which cannot be read is treated
 * as an image of the bit size of the launcher, because the creation of such a
 * process reports the missing file anyway.
 *
 * @param[in] image_path Host path of the image.
 * @return true when the image is a 32 bit image.
 */
bool Is32BitImage(const std::wstring& image_path)
{
    try
    {
        return appbox::PeImage::FromFile(image_path).Is32Bit();
    }
    catch (const std::exception&)
    {
        return sizeof(void*) == 4;
    }
}

/**
 * @brief Attribute list which names the job a process joins while it is created.
 *
 * The list is handed to the creation through the extended startup information,
 * so the process is already a member of the job before its first instruction
 * runs: the launcher cannot die between a creation and an assignment it would
 * make afterwards.
 *
 * The attribute keeps the address of the handle it was given, which the system
 * reads when the process is created, so the handle is a member of this object:
 * a handle of the caller, or a parameter of the call which builds the list,
 * would be gone by the time the process is created.
 */
class JobAttributeList
{
public:
    JobAttributeList() = default;

    ~JobAttributeList()
    {
        if (initialized_)
        {
            DeleteProcThreadAttributeList(info_.lpAttributeList);
        }
    }

    JobAttributeList(const JobAttributeList&) = delete;
    JobAttributeList& operator=(const JobAttributeList&) = delete;

    /**
     * @brief Build the list which names the job of the process.
     * @param[in] job Handle of the job the process joins.
     * @return true when the list is usable, false when the platform does not
     *         support the attribute, in which case the caller assigns the
     *         process to the job itself.
     */
    bool Build(HANDLE job)
    {
        ZeroMemory(&info_, sizeof(info_));
        info_.StartupInfo.cb = sizeof(info_);

        /*
         * The size query fails with ERROR_INSUFFICIENT_BUFFER by design: the
         * system reports the size the list needs, which the buffer is sized to.
         */
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (size == 0)
        {
            return false;
        }

        buffer_.resize(size);
        info_.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data());

        if (!InitializeProcThreadAttributeList(info_.lpAttributeList, 1, 0, &size))
        {
            info_.lpAttributeList = nullptr;
            return false;
        }
        initialized_ = true;

        job_ = job;

        return UpdateProcThreadAttribute(info_.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job_, sizeof(job_),
                                         nullptr, nullptr) != FALSE;
    }

    /**
     * @brief Get the startup information of the creation.
     * @return The startup information the list belongs to.
     */
    STARTUPINFOEXW& StartupInfo()
    {
        return info_;
    }

private:
    STARTUPINFOEXW    info_{};
    std::vector<BYTE> buffer_;
    HANDLE            job_ = nullptr;
    bool              initialized_ = false;
};

} // namespace

DWORD appbox::ProcessCreationFlags(bool hide_console, bool has_environment, bool job_in_creation)
{
    /*
     * The process is created suspended, because the sandbox module is injected
     * into it and the job is filled before the first instruction of the process
     * runs.
     */
    DWORD flags = CREATE_SUSPENDED;

    /*
     * A console program started by a program without a console gets a console
     * window of its own. The flag creates that console without the window, so
     * nothing appears on the desktop; the program keeps its console and
     * therefore its standard streams. A GUI program never owns a console
     * window and is not affected by the flag.
     */
    if (hide_console)
    {
        flags |= CREATE_NO_WINDOW;
    }

    /* An environment block which is given replaces the environment and is UTF-16. */
    if (has_environment)
    {
        flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    /* The attribute list of the job lives in the extended startup information. */
    if (job_in_creation)
    {
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    return flags;
}

struct appbox::ProcessJob::Data
{
    Data(const appbox::SandboxConfig& cfg, HANDLE completion_port);
    ~Data();

    HANDLE                    hIOCP;             /* Completion port the job reports to, owned by the set. */
    HANDLE                    hJob;              /* Job object. */
    PROCESS_INFORMATION       process_info;      /* Process information. */
    appbox::SandboxConfig     inject_data;       /* Injection data. */
    std::wstring              image_path;        /* Host path of the image to run. */
    std::wstring              command_line_path; /* Path the command line names. */
    std::vector<std::wstring> exe_args;          /* Target executable command line. */
    bool                      hide_console;      /* Start the target without a console window. */
    appbox::ProcessJobOptions options;           /* Environment and working directory of the target. */
    DWORD                     exit_code;         /* Exit code of target executable. */
};

appbox::ProcessJob::Data::Data(const appbox::SandboxConfig& cfg, HANDLE completion_port)
    : hIOCP(completion_port), hJob(nullptr), inject_data(cfg), hide_console(false), exit_code(0)
{
    ZeroMemory(&process_info, sizeof(process_info));

    hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob == nullptr)
    {
        SPDLOG_ERROR("failed to create the job object: {}", GetLastError());
        return;
    }

    {
        /*
         * The job kills every process it holds when its last handle closes, so
         * the processes of the sandbox cannot outlive the launcher. The job
         * allows no breakaway: a process of the job cannot leave it, which is
         * what keeps the wait of the run complete.
         */
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
        {
            SPDLOG_ERROR("failed to set the job object limit: {}", GetLastError());
        }
    }

    if (hIOCP != nullptr)
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

    /*
     * The completion port belongs to the set of the job, so it is not closed
     * here; closing the job handle kills the processes which are still in it.
     */
    if (hJob != nullptr)
    {
        CloseHandle(hJob);
        hJob = nullptr;
    }
}

appbox::ProcessJob::ProcessJob(const std::wstring image_path, const std::wstring command_line_path,
                               const std::vector<std::wstring> args, const appbox::SandboxConfig& inject_data,
                               bool hide_console, HANDLE completion_port, const appbox::ProcessJobOptions& options)
{
    data_ = new Data(inject_data, completion_port);
    data_->image_path = image_path;
    data_->command_line_path = command_line_path;
    data_->exe_args = args;
    data_->hide_console = hide_console;
    data_->options = options;
}

appbox::ProcessJob::~ProcessJob()
{
    delete data_;
}

DWORD appbox::ProcessJob::ProcessId() const
{
    return data_->process_info.dwProcessId;
}

DWORD appbox::ProcessJob::GetExitCode()
{
    DWORD code = 0;
    if (data_->process_info.hProcess != nullptr && GetExitCodeProcess(data_->process_info.hProcess, &code))
    {
        data_->exit_code = code;
    }

    return data_->exit_code;
}

HANDLE appbox::ProcessJob::JobHandle() const
{
    return data_->hJob;
}

DWORD appbox::ProcessJob::Terminate(DWORD exit_code)
{
    if (data_->hJob == nullptr)
    {
        return ERROR_INVALID_HANDLE;
    }

    /*
     * The job object stays alive and keeps reporting to the completion port of
     * the set: terminating only ends the processes, so the wait of the run
     * still sees the report of the job and its accounting stays correct.
     */
    if (!TerminateJobObject(data_->hJob, exit_code))
    {
        const DWORD err = GetLastError();
        SPDLOG_ERROR("failed to terminate the processes of the job: {}", err);
        return err;
    }

    return 0;
}

DWORD appbox::ProcessJob::Start()
{
    if (data_->hJob == nullptr)
    {
        SPDLOG_ERROR("the job object was not initialized");
        return ERROR_INVALID_HANDLE;
    }

    /*
     * The injected module has to match the bit size of the process, so a
     * program of the other bit size than the launcher is sandboxed as well.
     */
    const std::string& sandbox_dll_path =
        Is32BitImage(data_->image_path) ? data_->inject_data.sandbox32_dos_path : data_->inject_data.sandbox64_dos_path;

    /*
     * The command line names the path of the view, which is the path the
     * configuration spells and the path a sandboxed process would see, while
     * the image is the file of the layer which holds it: the launcher creates
     * the process itself, so it has to name both.
     */
    auto cmdline = appbox::BuildCommandLine(data_->command_line_path, data_->exe_args);

    /* The command line is what a reader of a failed run needs to reproduce it. */
    SPDLOG_DEBUG("create the process '{}' with '{}'", appbox::WideToUTF8(data_->image_path),
                 appbox::WideToUTF8(cmdline));

    /*
     * The environment block belongs to the options of the job, which outlive
     * the call: the creation only reads the block.
     */
    const bool    has_environment = !data_->options.environment.empty();
    LPVOID        environment = has_environment ? data_->options.environment.data() : nullptr;
    const LPCWSTR current_directory =
        data_->options.current_directory.empty() ? nullptr : data_->options.current_directory.c_str();

    /*
     * The job is named by an attribute of the creation, so the process is a
     * member of the job before it runs. A platform which does not support the
     * attribute creates the process the way it was created before the
     * attribute existed, and the process is assigned to the job below.
     */
    JobAttributeList attributes;
    bool             job_in_creation = attributes.Build(data_->hJob);
    if (!job_in_creation)
    {
        SPDLOG_INFO("the creation of the process cannot name the job: {}", GetLastError());
    }

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    auto create = [&](bool name_job) -> BOOL {
        const DWORD flags = appbox::ProcessCreationFlags(data_->hide_console, has_environment, name_job);
        if (name_job)
        {
            return CreateProcessW(data_->image_path.c_str(), cmdline.data(), nullptr, nullptr, FALSE, flags,
                                  environment, current_directory, &attributes.StartupInfo().StartupInfo, &process_info);
        }

        /*
         * The creation which injects the module itself takes the plain startup
         * information: the attribute list of the job is not part of it.
         */
        return DetourCreateProcessWithDllExW(data_->image_path.c_str(), cmdline.data(), nullptr, nullptr, false, flags,
                                             environment, current_directory, &startup_info, &process_info,
                                             sandbox_dll_path.c_str(), nullptr);
    };

    BOOL created = create(job_in_creation);
    if (!created && job_in_creation && GetLastError() == ERROR_INVALID_PARAMETER)
    {
        /*
         * The system refuses the attribute although the list was built, which
         * is a platform which does not support the job attribute at all: the
         * process is created without it and assigned to the job below.
         */
        SPDLOG_INFO("the creation of the process refused the job attribute: {}", GetLastError());
        job_in_creation = false;
        created = create(false);
    }

    if (!created)
    {
        const DWORD err = GetLastError();
        SPDLOG_ERROR("failed to create the process: {}", err);
        return err;
    }

    data_->process_info = process_info;

    auto fail = [this](DWORD err) -> DWORD {
        data_->exit_code = err;
        TerminateProcess(data_->process_info.hProcess, err);
        return err;
    };

    if (job_in_creation)
    {
        /*
         * The module is injected into the suspended process, like the creation
         * of Detours does for a creation which does not name a job: the import
         * table of the process is patched before the process runs. A process of
         * the other bit size cannot be patched by this process, so the helper
         * of Detours patches it, exactly like the creation of Detours does.
         */
        LPCSTR dlls[1] = { sandbox_dll_path.c_str() };
        if (!DetourUpdateProcessWithDll(data_->process_info.hProcess, dlls, 1) &&
            !DetourProcessViaHelperW(data_->process_info.dwProcessId, sandbox_dll_path.c_str(), CreateProcessW))
        {
            const DWORD err = GetLastError();
            SPDLOG_ERROR("failed to inject the sandbox module: {}", err);
            return fail(err);
        }
    }

    const GUID  guid = APPBOX_SANDBOX_GUID;
    std::string inject_data = nlohmann::json(data_->inject_data).dump();
    DWORD       inject_data_sz = static_cast<DWORD>(inject_data.size());
    if (!DetourCopyPayloadToProcess(data_->process_info.hProcess, guid, inject_data.c_str(), inject_data_sz))
    {
        const DWORD err = GetLastError();
        SPDLOG_ERROR("failed to write the injection data of the process: {}", err);
        return fail(err);
    }

    if (!job_in_creation && !AssignProcessToJobObject(data_->hJob, data_->process_info.hProcess))
    {
        const DWORD err = GetLastError();
        SPDLOG_ERROR("failed to assign the process to the job: {}", err);
        return fail(err);
    }

    if (ResumeThread(data_->process_info.hThread) == static_cast<DWORD>(-1))
    {
        const DWORD err = GetLastError();
        SPDLOG_ERROR("failed to resume the target process: {}", err);
        return fail(err);
    }

    CloseHandle(data_->process_info.hThread);
    data_->process_info.hThread = nullptr;

    return 0;
}
