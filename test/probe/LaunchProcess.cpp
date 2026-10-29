#include "WinAPI.h" /* Must be first include file */
#include "LaunchProcess.hpp"
#include "WString.hpp"
#include <iterator>
#include <vector>

namespace
{

/* The flags of the parameters a caller builds for the runtime of the system. */
constexpr ULONG kRtlUserProcParamsNormalized = 0x00000001;

/**
 * @brief The record `RtlCreateUserProcess` reports the new process in.
 *
 * The structure is not part of the headers of the SDK, so it is declared here
 * with the layout the runtime of the system fills: the two handles are followed
 * by the client id of the process and by the image information, which is filled
 * as well. The image information is declared as storage of its own size,
 * because the call writes it and a shorter record would be overwritten past its
 * end.
 */
struct RTL_USER_PROCESS_INFORMATION
{
    ULONG  Length;
    HANDLE Process;
    HANDLE Thread;

    /* The client id of the new process: its process and its thread. */
    struct
    {
        HANDLE UniqueProcess;
        HANDLE UniqueThread;
    } ClientId;

    ULONG_PTR ImageInformation[16]; /* SECTION_IMAGE_INFORMATION, opaque here. */
};

/* clang-format off */
typedef NTSTATUS(NTAPI* T_RtlCreateProcessParametersEx)(
    /* [OUT] */             PRTL_USER_PROCESS_PARAMETERS* ProcessParameters,
    /* [IN] */              PUNICODE_STRING             ImagePathName,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             DllPath,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             CurrentDirectory,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             CommandLine,
    /* [IN,OPTIONAL] */     PVOID                       Environment,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             WindowTitle,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             DesktopInfo,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             ShellInfo,
    /* [IN,OPTIONAL] */     PUNICODE_STRING             RuntimeData,
    /* [IN] */              ULONG                       Flags);

typedef NTSTATUS(NTAPI* T_RtlDestroyProcessParameters)(
    /* [IN] */ PRTL_USER_PROCESS_PARAMETERS ProcessParameters);

typedef NTSTATUS(NTAPI* T_RtlCreateUserProcess)(
    /* [IN] */              PUNICODE_STRING                 ImagePathName,
    /* [IN] */              ULONG                           Attributes,
    /* [IN,OPTIONAL] */     PRTL_USER_PROCESS_PARAMETERS    ProcessParameters,
    /* [IN,OPTIONAL] */     PSECURITY_DESCRIPTOR            ProcessSecurityDescriptor,
    /* [IN,OPTIONAL] */     PSECURITY_DESCRIPTOR            ThreadSecurityDescriptor,
    /* [IN] */              BOOLEAN                         InheritHandles,
    /* [IN,OPTIONAL] */     HANDLE                          ParentProcess,
    /* [IN] */              BOOLEAN                         InheritFromParent,
    /* [IN,OPTIONAL] */     HANDLE                          ParentThread,
    /* [OUT] */             RTL_USER_PROCESS_INFORMATION*   ProcessInformation);
/* clang-format on */

/**
 * @brief Fill a counted string over the buffer of a text.
 *
 * The text has to outlive the call, because the structure only names its
 * buffer.
 *
 * @param[in] text The text of the string.
 * @return The counted string.
 */
UNICODE_STRING MakeUnicodeString(const std::wstring& text)
{
    UNICODE_STRING string;
    string.Buffer = const_cast<PWSTR>(text.c_str());
    string.Length = static_cast<USHORT>(text.size() * sizeof(wchar_t));
    string.MaximumLength = static_cast<USHORT>((text.size() + 1) * sizeof(wchar_t));
    return string;
}

/**
 * @brief Turn a status of the runtime of the system into an error code.
 *
 * @param[in] status The status of the call.
 * @return The error code the case reports.
 */
DWORD StatusToErrorCode(NTSTATUS status)
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return ERROR_GEN_FAILURE;
    }

    auto to_dos_error = reinterpret_cast<T_RtlNtStatusToDosError>(GetProcAddress(ntdll, "RtlNtStatusToDosError"));
    if (to_dos_error == nullptr)
    {
        return ERROR_GEN_FAILURE;
    }

    return to_dos_error(status);
}

/**
 * @brief Launch a program through the runtime of the system.
 *
 * The parameters of the child are built with `RtlCreateProcessParametersEx` and
 * the process with `RtlCreateUserProcess`, which reaches `NtCreateUserProcess`
 * without passing the Win32 creation code.
 *
 * @param[in] file Path of the image in UTF-8.
 * @param[in] command_line Command line of the child.
 * @param[in] current_directory Working directory, empty to inherit.
 * @param[out] exit_code Exit code of the child.
 * @return Error code of the creation, zero on success.
 */
DWORD LaunchThroughRtlCreateUserProcess(const std::wstring& file, const std::wstring& command_line,
                                        const std::wstring& current_directory, DWORD& exit_code)
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return ERROR_MOD_NOT_FOUND;
    }

    auto dos_path_to_nt_path = reinterpret_cast<T_RtlDosPathNameToNtPathName_U_WithStatus>(
        GetProcAddress(ntdll, "RtlDosPathNameToNtPathName_U_WithStatus"));
    auto free_unicode_string = reinterpret_cast<T_RtlFreeUnicodeString>(GetProcAddress(ntdll, "RtlFreeUnicodeString"));
    auto create_parameters =
        reinterpret_cast<T_RtlCreateProcessParametersEx>(GetProcAddress(ntdll, "RtlCreateProcessParametersEx"));
    auto destroy_parameters =
        reinterpret_cast<T_RtlDestroyProcessParameters>(GetProcAddress(ntdll, "RtlDestroyProcessParameters"));
    auto create_process = reinterpret_cast<T_RtlCreateUserProcess>(GetProcAddress(ntdll, "RtlCreateUserProcess"));

    if (dos_path_to_nt_path == nullptr || free_unicode_string == nullptr || create_parameters == nullptr ||
        destroy_parameters == nullptr || create_process == nullptr)
    {
        return ERROR_PROC_NOT_FOUND;
    }

    UNICODE_STRING image_path;
    ZeroMemory(&image_path, sizeof(image_path));
    NTSTATUS status = dos_path_to_nt_path(file.c_str(), &image_path, nullptr, nullptr);
    if (!NT_SUCCESS(status))
    {
        return StatusToErrorCode(status);
    }

    /*
     * A caller which names no directory hands the child the directory of its
     * own, which is what the Win32 creation code does for the same request.
     */
    std::wstring inherited_directory;
    if (current_directory.empty())
    {
        wchar_t     buffer[MAX_PATH * 4];
        const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(std::size(buffer)), buffer);
        if (length > 0 && length < std::size(buffer))
        {
            inherited_directory.assign(buffer, length);
        }
    }

    const std::wstring& directory = current_directory.empty() ? inherited_directory : current_directory;

    UNICODE_STRING command_line_string = MakeUnicodeString(command_line);
    UNICODE_STRING directory_string = MakeUnicodeString(directory);

    PRTL_USER_PROCESS_PARAMETERS parameters = nullptr;
    status = create_parameters(&parameters, &image_path, nullptr, directory.empty() ? nullptr : &directory_string,
                               &command_line_string, nullptr, nullptr, nullptr, nullptr, nullptr,
                               kRtlUserProcParamsNormalized);

    if (!NT_SUCCESS(status))
    {
        free_unicode_string(&image_path);
        return StatusToErrorCode(status);
    }

    RTL_USER_PROCESS_INFORMATION information;
    ZeroMemory(&information, sizeof(information));
    information.Length = sizeof(information);

    status = create_process(&image_path, OBJ_CASE_INSENSITIVE, parameters, nullptr, nullptr, FALSE, nullptr, FALSE,
                            nullptr, &information);

    destroy_parameters(parameters);
    free_unicode_string(&image_path);

    if (!NT_SUCCESS(status))
    {
        return StatusToErrorCode(status);
    }

    /* The runtime of the system creates the thread suspended. */
    ResumeThread(information.Thread);

    WaitForSingleObject(information.Process, INFINITE);
    GetExitCodeProcess(information.Process, &exit_code);

    CloseHandle(information.Thread);
    CloseHandle(information.Process);
    return ERROR_SUCCESS;
}

/**
 * @brief Launch a program through `CreateProcessW`.
 *
 * @param[in] file Path of the image in UTF-8.
 * @param[in] command_line Command line of the child.
 * @param[in] current_directory Working directory, empty to inherit.
 * @param[out] exit_code Exit code of the child.
 * @return Error code of the creation, zero on success.
 */
DWORD LaunchThroughCreateProcess(const std::wstring& file, std::wstring& command_line,
                                 const std::wstring& current_directory, DWORD& exit_code)
{
    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');

    STARTUPINFOW        startup_info;
    PROCESS_INFORMATION process_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    ZeroMemory(&process_info, sizeof(process_info));

    if (!CreateProcessW(file.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        current_directory.empty() ? nullptr : current_directory.c_str(), &startup_info, &process_info))
    {
        return GetLastError();
    }

    CloseHandle(process_info.hThread);

    WaitForSingleObject(process_info.hProcess, INFINITE);
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hProcess);

    return ERROR_SUCCESS;
}

} // namespace

static nlohmann::json ProbeLaunchProcess_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolLaunchProcess::Req>();

    appbox::test::ProtocolLaunchProcess::Rsp rsp;

    /*
     * A case which checks what a child makes of the directory it inherits puts
     * the probe into the directory first: the child inherits the directory of
     * the probe, not the one the request names.
     */
    if (!req.ParentCurrentDirectory.empty())
    {
        if (!SetCurrentDirectoryW(appbox::UTF8ToWide(req.ParentCurrentDirectory).c_str()))
        {
            rsp.code = GetLastError();
            return rsp;
        }
    }

    const std::wstring file = appbox::UTF8ToWide(req.FileName);

    /* Build the command line: "<exe>" <arg> <arg> ... The arguments are
     * joined with plain spaces, quoting stays under the caller's control. */
    std::wstring cmdline = L"\"" + file + L"\"";
    for (const auto& arg : req.Arguments)
    {
        cmdline += L" ";
        cmdline += appbox::UTF8ToWide(arg);
    }

    const std::wstring current_directory = appbox::UTF8ToWide(req.CurrentDirectory);

    switch (req.Api)
    {
    case appbox::test::LaunchApi::RtlCreateUserProcess:
        rsp.code = LaunchThroughRtlCreateUserProcess(file, cmdline, current_directory, rsp.exit_code);
        break;

    case appbox::test::LaunchApi::CreateProcess:
    default:
        rsp.code = LaunchThroughCreateProcess(file, cmdline, current_directory, rsp.exit_code);
        break;
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeLaunchProcess("LaunchProcess", ProbeLaunchProcess_Entry);
