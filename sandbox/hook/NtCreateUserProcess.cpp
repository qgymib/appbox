#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/Defines.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "utils/ProcessParameters.hpp"
#include "environment/Isolation.hpp"
#include "filesystem/LayerPath.hpp"
#include "filesystem/Resolve.hpp"
#include "Sandbox.hpp"
#include "NtCreateUserProcess.hpp"
#include "WString.hpp"
#include <detours.h>

T_NtCreateUserProcess sys_NtCreateUserProcess = nullptr;

static nlohmann::json NtCreateUserProcessLogParam(PHANDLE ProcessHandle, PHANDLE ThreadHandle,
                                                  ACCESS_MASK ProcessDesiredAccess, ACCESS_MASK ThreadDesiredAccess,
                                                  POBJECT_ATTRIBUTES ProcessObjectAttributes,
                                                  POBJECT_ATTRIBUTES ThreadObjectAttributes, ULONG ProcessFlags,
                                                  ULONG ThreadFlags, PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
                                                  PPS_CREATE_INFO CreateInfo, PPS_ATTRIBUTE_LIST AttributeList)
{
    nlohmann::json param;
    param["ProcessHandle"] = appbox::PointerToString(ProcessHandle);
    param["ThreadHandle"] = appbox::PointerToString(ThreadHandle);
    param["ProcessDesiredAccess"] = ProcessDesiredAccess;
    param["ThreadDesiredAccess"] = ThreadDesiredAccess;
    param["ProcessObjectAttributes"] = appbox::PointerToString(ProcessObjectAttributes);
    param["ThreadObjectAttributes"] = appbox::PointerToString(ThreadObjectAttributes);
    param["ProcessFlags"] = ProcessFlags;
    param["ThreadFlags"] = ThreadFlags;
    param["ProcessParameters"] = appbox::PointerToString(ProcessParameters);
    param["CreateInfo"] = appbox::PointerToString(CreateInfo);
    param["AttributeList"] = appbox::PointerToString(AttributeList);

    /*
     * The image of the process is the one value of the call which describes
     * what the caller asks for, so it is the one the log carries.
     */
    appbox::process_parameters::ImageNameAttribute image(AttributeList);
    std::wstring                                   image_path;
    if (image.GetName(image_path))
    {
        param["ImagePathName"] = appbox::WideToUTF8(image_path);
    }

    return param;
}
static appbox::LoggerF logger("NtCreateUserProcess", NtCreateUserProcessLogParam);

namespace
{

/**
 * @brief Mark the current thread as being inside this hook.
 *
 * The cross-bitness injection of Detours starts a helper process of its own
 * (`rundll32.exe` with the injection module of the other bit size) through
 * `CreateProcessW`, which reaches this hook again. The helper only patches the
 * import table of the target process and leaves, so it is not part of the
 * sandbox: the nested creation is forwarded unchanged, and the mark tells the
 * hook that the creation is the nested one.
 */
struct HookLock
{
    HookLock()
    {
        appbox::ThreadLocal::Get().disable_NtCreateUserProcess_hook = true;
    }

    ~HookLock()
    {
        appbox::ThreadLocal::Get().disable_NtCreateUserProcess_hook = false;
    }
};

/**
 * @brief Resolve the image path of a creation into the layer which holds it.
 *
 * The path the caller names is a path of the view, while the kernel opens the
 * image in the host filesystem: a view path which only a layer holds has to be
 * rewritten into the path of that layer before the call is forwarded.
 *
 * @param[in] nt_path Image path the caller named, in the object namespace.
 * @param[out] view_path Path of the view the image belongs to.
 * @param[out] layer_path Host path which holds the image.
 * @param[out] reparse_failure Status of a reparse point of the path which the
 *                             view could not resolve, `STATUS_SUCCESS`
 *                             otherwise.
 * @return true when the path was resolved and should be rewritten.
 */
bool ResolveImagePath(const std::wstring& nt_path, std::wstring& view_path, std::wstring& layer_path,
                      NTSTATUS& reparse_failure)
{
    reparse_failure = STATUS_SUCCESS;

    std::wstring dos_nt_path;
    if (!appbox::MappingAsDosNtPath(nt_path, dos_nt_path))
    {
        return false;
    }

    auto result = appbox::filesystem::Resolve(dos_nt_path);

    /*
     * A reparse point of the image path which the view could not resolve fails
     * the creation: the forwarded call resolves the path in the host
     * filesystem and would load an image the view never decided about.
     */
    if (!NT_SUCCESS(result->reparseStatus))
    {
        reparse_failure = result->reparseStatus;
        return false;
    }

    if (result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        /* The image does not exist in the view either, let the original call
         * produce the failure. */
        return false;
    }

    layer_path = result->bInUpper ? result->uPath : result->hPath[0].fPath;
    view_path = result->viewPath.empty() ? dos_nt_path : result->viewPath;
    return true;
}

/**
 * @brief Map the working directory of a creation back into the view.
 *
 * A child inherits the working directory of its parent, and the directory of a
 * sandboxed process names a layer once the view is mounted: the parameters of
 * the child therefore carry the path of a layer, which is the path the sandbox
 * maps back into the view here. A directory which names no layer is kept as the
 * caller spelled it.
 *
 * @param[in] directory Working directory the parameters carry.
 * @return The path of the view, empty when the directory is no path of a layer.
 */
std::wstring ViewPathOfDirectory(const std::wstring& directory)
{
    if (directory.empty())
    {
        return {};
    }

    /*
     * The parameters carry the path of a process, which is either the path of
     * the object namespace or a path with the drive of the view; the rebase
     * compares the paths of the object namespace, so a drive path is turned
     * into one first. A relative directory names no layer.
     */
    std::wstring nt_path;
    if (directory[0] == L'\\')
    {
        nt_path = directory;
    }
    else if (directory.size() > 1 && directory[1] == L':')
    {
        nt_path = L"\\??\\" + directory;
    }
    else
    {
        return {};
    }

    std::wstring view_path;
    if (!appbox::filesystem::RebaseLayerPathToView(nt_path, view_path))
    {
        return {};
    }

    return appbox::process_parameters::ToDosPath(view_path);
}

/**
 * @brief Inject the sandbox module into a process the call created.
 *
 * The rights the caller asked for need not include the ones an injection
 * writes with, so the module opens a handle of its own. A target of the other
 * bit size is injected through the helper process of Detours, whose own
 * creation reaches this hook and is forwarded unchanged.
 *
 * @param[in] process Handle of the new process the call reported.
 * @param[in] inject_data Configuration of the sandbox for the new process.
 * @return `STATUS_SUCCESS`, or the failure of the injection.
 */
NTSTATUS InjectSandbox(HANDLE process, const std::string& inject_data)
{
#if defined(_WIN64)
    LPCSTR dll_name = appbox::sandbox->sandbox64_dos_path.c_str();
#else
    LPCSTR dll_name = appbox::sandbox->sandbox32_dos_path.c_str();
#endif

    const DWORD process_id = process != nullptr ? GetProcessId(process) : 0;
    if (process_id == 0)
    {
        return STATUS_INVALID_HANDLE;
    }

    HANDLE injection = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);
    HANDLE target = injection != nullptr ? injection : process;

    bool injected = DetourUpdateProcessWithDll(target, &dll_name, 1) != FALSE ||
                    DetourProcessViaHelperW(process_id, dll_name, CreateProcessW) != FALSE;

    if (injected)
    {
        const GUID guid = APPBOX_SANDBOX_GUID;
        const auto size = static_cast<DWORD>(inject_data.size());
        injected = DetourCopyPayloadToProcess(target, guid, inject_data.c_str(), size) != FALSE;
    }

    if (injection != nullptr)
    {
        CloseHandle(injection);
    }

    return injected ? STATUS_SUCCESS : STATUS_DLL_INIT_FAILED;
}

/**
 * @brief Remove a process the sandbox could not enter.
 *
 * The call which created the process reported it as created, so the sandbox
 * has to take it back: the process is terminated and the handles of the call
 * are closed, which is what the caller of the original call expects of a
 * creation which failed.
 *
 * @param[in] process Handle of the new process.
 * @param[in,out] process_handle The handle the call reported.
 * @param[in,out] thread_handle The handle of the first thread the call reported.
 */
void TerminateAndClose(HANDLE process, PHANDLE process_handle, PHANDLE thread_handle)
{
    const DWORD process_id = process != nullptr ? GetProcessId(process) : 0;
    if (process_id != 0)
    {
        HANDLE own = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
        if (own != nullptr)
        {
            TerminateProcess(own, static_cast<UINT>(STATUS_DLL_INIT_FAILED));
            CloseHandle(own);
        }
    }

    if (process_handle != nullptr && *process_handle != nullptr)
    {
        CloseHandle(*process_handle);
        *process_handle = nullptr;
    }

    if (thread_handle != nullptr && *thread_handle != nullptr)
    {
        CloseHandle(*thread_handle);
        *thread_handle = nullptr;
    }
}

} // namespace

static NTSTATUS NTAPI Hook_NtCreateUserProcess(PHANDLE ProcessHandle, PHANDLE ThreadHandle,
                                               ACCESS_MASK ProcessDesiredAccess, ACCESS_MASK ThreadDesiredAccess,
                                               POBJECT_ATTRIBUTES ProcessObjectAttributes,
                                               POBJECT_ATTRIBUTES ThreadObjectAttributes, ULONG ProcessFlags,
                                               ULONG ThreadFlags, PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
                                               PPS_CREATE_INFO CreateInfo, PPS_ATTRIBUTE_LIST AttributeList)
{
    /*
     * The helper process of the cross-bitness injection is created from inside
     * this hook (see HookLock): the call is forwarded unchanged, without the
     * rewrite of the image path and without an injection of its own.
     */
    if (appbox::ThreadLocal::Get().disable_NtCreateUserProcess_hook)
    {
        return sys_NtCreateUserProcess(ProcessHandle, ThreadHandle, ProcessDesiredAccess, ThreadDesiredAccess,
                                       ProcessObjectAttributes, ThreadObjectAttributes, ProcessFlags, ThreadFlags,
                                       ProcessParameters, CreateInfo, AttributeList);
    }

    HookLock lock;

    logger.Log(ProcessHandle, ThreadHandle, ProcessDesiredAccess, ThreadDesiredAccess, ProcessObjectAttributes,
               ThreadObjectAttributes, ProcessFlags, ThreadFlags, ProcessParameters, CreateInfo, AttributeList);

    /*
     * The image of the process is named by the attribute list. A caller which
     * builds the parameters itself may name it there alone, so the parameters
     * are the second place the name is read from.
     */
    appbox::process_parameters::ImageNameAttribute image_name(AttributeList);

    std::wstring image_path;
    const bool   bFromAttribute = image_name.IsValid() && image_name.GetName(image_path);

    if (!bFromAttribute && ProcessParameters != nullptr)
    {
        const UNICODE_STRING& name = ProcessParameters->ImagePathName;
        if (name.Buffer != nullptr && name.Length != 0)
        {
            image_path.assign(name.Buffer, static_cast<std::size_t>(name.Length) / sizeof(wchar_t));
        }
    }

    std::wstring view_path;
    std::wstring layer_path;
    NTSTATUS     reparse_failure = STATUS_SUCCESS;

    const bool bResolved = !image_path.empty() && ResolveImagePath(image_path, view_path, layer_path, reparse_failure);
    if (!NT_SUCCESS(reparse_failure))
    {
        LOG_W(L"failed to resolve the reparse point of the image {}: {}", image_path, reparse_failure);
        return reparse_failure;
    }

    /*
     * The parameters of the caller are the values the kernel reads while the
     * call runs: the sandbox installs the values of the view for the duration
     * of the call and the objects restore the values of the caller afterwards.
     */
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    {
        appbox::process_parameters::ViewParameters parameters(ProcessParameters);

        if (bResolved)
        {
            if (bFromAttribute)
            {
                /* The kernel loads the image from the layer, the process
                 * reports the path of the view. */
                image_name.Set(layer_path);
                parameters.SetImagePath(appbox::process_parameters::ToDosPath(view_path));
            }
            else
            {
                parameters.SetImagePath(appbox::process_parameters::ToDosPath(layer_path));
            }
        }

        const std::wstring directory = appbox::process_parameters::GetCurrentDirectory(ProcessParameters);
        const std::wstring view_directory = ViewPathOfDirectory(directory);
        LOG_T(L"working directory of the child: '{}' -> '{}'", directory, view_directory);
        parameters.SetCurrentDirectory(view_directory);

        status = sys_NtCreateUserProcess(ProcessHandle, ThreadHandle, ProcessDesiredAccess, ThreadDesiredAccess,
                                         ProcessObjectAttributes, ThreadObjectAttributes, ProcessFlags, ThreadFlags,
                                         ProcessParameters, CreateInfo, AttributeList);
    }

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    /*
     * The environment of the child is the environment of this process, which is
     * the block of the sandbox: a caller which inherits receives the composed
     * environment, and every child is told that its environment is composed
     * already, so the configuration is not applied to it a second time: the
     * values the merge modes join would be joined twice.
     */
    std::string        child_inject_data;
    const std::string* inject_data = &appbox::sandbox->inject_data;

    if (appbox::environment::Isolation::IsEnabled())
    {
        child_inject_data = appbox::environment::BuildChildInjectData();
        inject_data = &child_inject_data;
    }

    const HANDLE   process = ProcessHandle != nullptr ? *ProcessHandle : nullptr;
    const NTSTATUS injection = InjectSandbox(process, *inject_data);
    if (!NT_SUCCESS(injection))
    {
        LOG_E("failed to inject the sandbox into the process of {}", appbox::WideToUTF8(image_path));
        TerminateAndClose(process, ProcessHandle, ThreadHandle);
        return injection;
    }

    return status;
}

static void LoadNtCreateUserProcess()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtCreateUserProcess");
    sys_NtCreateUserProcess = reinterpret_cast<T_NtCreateUserProcess>(addr);
}

appbox::HookRecord appbox::HookNtCreateUserProcess = {
    "NtCreateUserProcess",
    LoadNtCreateUserProcess,
    (void**)&sys_NtCreateUserProcess,
    Hook_NtCreateUserProcess,
};
