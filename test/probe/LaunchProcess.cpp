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
 * @brief The prefix of `RTL_USER_PROCESS_PARAMETERS` which decides the window.
 *
 * The structure is not part of the headers of the SDK, so the members the
 * creation of a child needs are declared here with the layout the runtime of
 * the system fills: the counted length of the block, the flags of the block,
 * the handle and the flags of the console, the three standard streams, the
 * counted strings of the block and the window flags. The `static_assert` below
 * pins the offsets, so a layout the runtime of the system changes is a failure
 * of the build instead of a write past the field.
 *
 * A block built by `RtlCreateProcessParametersEx` names no window flags, which
 * the runtime of the system reads as `SW_SHOWDEFAULT`: a child of a process
 * without a console opens a console window then. A caller which sets
 * `WindowFlags` and `ShowWindowFlags` says that the window is hidden instead,
 * which is the countermovement of `CREATE_NO_WINDOW` on the Win32 path. The
 * console flags carry the same statement, so a runtime of the system which
 * creates the console of the child itself keeps the window hidden as well.
 */
struct RTL_USER_PROCESS_PARAMETERS_PREFIX
{
    ULONG          MaximumLength;
    ULONG          Length;
    ULONG          Flags;
    ULONG          DebugFlags;
    HANDLE         ConsoleHandle;
    ULONG          ConsoleFlags;
    HANDLE         StandardInput;
    HANDLE         StandardOutput;
    HANDLE         StandardError;
    UNICODE_STRING CurrentDirectory;
    HANDLE         CurrentDirectoryHandle;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PWSTR          Environment;
    ULONG          StartingPositionLeft;
    ULONG          StartingPositionTop;
    ULONG          Width;
    ULONG          Height;
    ULONG          CharWidth;
    ULONG          CharHeight;
    ULONG          ConsoleTextAttributes;
    ULONG          WindowFlags;
    ULONG          ShowWindowFlags;
};

/*
 * The offsets the block of the runtime of the system carries, per bit size: the
 * counted strings of the block are as wide as a pointer of the process, so the
 * window members of the 32 bit block start four counted strings earlier than
 * the ones of the 64 bit block. A member which the runtime of the system adds,
 * removes or realigns breaks one of the two assertions instead of turning the
 * write of the window flags into a write past the block.
 */
#if defined(_WIN64)
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, ConsoleHandle) == 0x10,
              "the console handle of the process parameters moved");
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, Width) == 0x90,
              "the window position of the process parameters moved");
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, WindowFlags) == 0xA4,
              "the window flags of the process parameters moved");
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, ShowWindowFlags) == 0xA8,
              "the show window flags of the process parameters moved");
#else
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, ConsoleHandle) == 0x10,
              "the console handle of the process parameters moved");
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, Width) == 0x54,
              "the window position of the process parameters moved");
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, WindowFlags) == 0x68,
              "the window flags of the process parameters moved");
static_assert(offsetof(RTL_USER_PROCESS_PARAMETERS_PREFIX, ShowWindowFlags) == 0x6C,
              "the show window flags of the process parameters moved");
#endif

/* The block carries the window fields, so the runtime of the system reads them. */
constexpr ULONG kRtlUserProcParamsWindowFlags = 0x00000080;

/* The flag of the block which keeps the console the block inherits. */
constexpr ULONG kRtlUserProcParamsConsoleNoWindow = 0x00000002;

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

    /*
     * A caller of this path reaches `NtCreateUserProcess` without the Win32
     * creation code, so it carries no `STARTUPINFO` and no `CREATE_NO_WINDOW`:
     * the window of the child is what the parameters of the block say. The
     * probe itself owns a console whose window is hidden, so a child which
     * inherited that reading would open a console window of its own and show
     * it on the desktop. The block is told to hide the window instead, which
     * keeps the console of the child, and therefore its standard streams, and
     * keeps the window off the desktop of the machine the cases run on.
     */
    auto prefix = reinterpret_cast<RTL_USER_PROCESS_PARAMETERS_PREFIX*>(parameters);
    prefix->WindowFlags |= kRtlUserProcParamsWindowFlags;
    prefix->ShowWindowFlags = SW_HIDE;
    prefix->ConsoleFlags |= kRtlUserProcParamsConsoleNoWindow;

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
 * The child is created without a console window: the probe runs in a sandbox
 * whose launcher starts it without a console window as well, so a child created
 * with the default flags would be given a console of its own and would show it
 * on the desktop of the machine the cases run on. The flag creates that console
 * hidden instead, so the child keeps its console and its standard streams, and
 * `startup_info` is left to its defaults, which keeps the handles of the probe
 * out of the child.
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

    if (!CreateProcessW(file.c_str(), buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
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

appbox::test::ProtocolLaunchProcess::Rsp appbox::test::ProbeLaunchProcess_Entry(
    const appbox::test::ProtocolLaunchProcess::Req& request)
{
    const auto& req = request;

    appbox::test::ProtocolLaunchProcess::Rsp rsp;

    /*
     * A case which checks what a child makes of the directory it inherits puts
     * the probe into the directory first: the child inherits the directory of
     * the probe, not the one the request names.
     */
    if (!req.ParentCurrentDirectory.empty())
    {
        const std::wstring parent_directory = appbox::UTF8ToWide(req.ParentCurrentDirectory).value_or(std::wstring());
        if (!SetCurrentDirectoryW(parent_directory.c_str()))
        {
            rsp.code = GetLastError();
            return rsp;
        }
    }

    const std::wstring file = appbox::UTF8ToWide(req.FileName).value_or(std::wstring());

    /* Build the command line: "<exe>" <arg> <arg> ... The arguments are
     * joined with plain spaces, quoting stays under the caller's control. */
    std::wstring cmdline = L"\"" + file + L"\"";
    for (const auto& arg : req.Arguments)
    {
        cmdline += L" ";
        cmdline += appbox::UTF8ToWide(arg).value_or(std::wstring());
    }

    const std::wstring current_directory = appbox::UTF8ToWide(req.CurrentDirectory).value_or(std::wstring());

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
