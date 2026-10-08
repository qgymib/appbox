#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <stdexcept>
#include <utility>
#include <spdlog/spdlog.h>
#include "hook/__init__.hpp"
#include "hook/NtCreateFile.hpp"
#include "environment/Isolation.hpp"
#include "filesystem/Isolation.hpp"
#include "fonts/Isolation.hpp"
#include "network/Isolation.hpp"
#include "registry/__init__.hpp"
#include "utils/CrashReport.hpp"
#include "utils/Defines.hpp"
#include "utils/GetPEB.hpp"
#include "utils/HandleInfo.hpp"
#include "utils/Log.hpp"
#include "ModuleTable.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"

static const appbox::ModuleInitializer s_module[] = {
    { appbox::HandleInfo::Init,             appbox::HandleInfo::Exit             },
    { appbox::registry::Hive::Init,         appbox::registry::Hive::Exit         },
    { appbox::filesystem::Isolation::Init,  appbox::filesystem::Isolation::Exit  },
    { appbox::fonts::Isolation::Init,       appbox::fonts::Isolation::Exit       },
    { appbox::network::Isolation::Init,     appbox::network::Isolation::Exit     },
    { appbox::environment::Isolation::Init, appbox::environment::Isolation::Exit },
    { appbox::InitHook,                     appbox::ExitHook                     },
};

appbox::Sandbox* appbox::sandbox = nullptr;

static void ParseInjectData(const std::string& data)
{
    appbox::SandboxConfig inject_data;
    nlohmann::json::parse(data).get_to(inject_data);

    appbox::sandbox->sandbox32_dos_path = inject_data.sandbox32_dos_path;
    appbox::sandbox->sandbox64_dos_path = inject_data.sandbox64_dos_path;
    appbox::sandbox->wPipePath = appbox::UTF8ToWide(inject_data.pipe_path);

    appbox::sandbox->fs.fs_upper = appbox::UTF8ToWide(inject_data.fs_upper);
    for (const auto& p : inject_data.fs_lower)
    {
        appbox::filesystem::ResolveFsMapping mapping;
        mapping.host_nt_path = appbox::UTF8ToWide(p.host_nt_path);
        mapping.mapped_nt_path = appbox::UTF8ToWide(p.mapped_nt_path);
        appbox::sandbox->fs.fs_lower.push_back(mapping);
    }

    for (const auto& v : inject_data.variables)
    {
        appbox::VariableMapping variable;
        variable.name = appbox::UTF8ToWide(v.name);
        variable.path = appbox::UTF8ToWide(v.path);
        appbox::sandbox->variables.push_back(std::move(variable));
    }

    appbox::sandbox->wRegistryHiveDOSPath = appbox::UTF8ToWide(inject_data.registry_hive_dos_path);
    for (const auto& path : inject_data.registry_isolation_dos_paths)
    {
        appbox::sandbox->wRegistryIsolationDOSPaths.push_back(appbox::UTF8ToWide(path));
    }
    for (const auto& path : inject_data.filesystem_isolation_dos_paths)
    {
        appbox::sandbox->wFilesystemIsolationDOSPaths.push_back(appbox::UTF8ToWide(path));
    }
    for (const auto& path : inject_data.network_isolation_dos_paths)
    {
        appbox::sandbox->wNetworkIsolationDOSPaths.push_back(appbox::UTF8ToWide(path));
    }
    for (const auto& path : inject_data.environment_isolation_dos_paths)
    {
        appbox::sandbox->wEnvironmentIsolationDOSPaths.push_back(appbox::UTF8ToWide(path));
    }
    appbox::sandbox->wEnvironmentStateDOSPath = appbox::UTF8ToWide(inject_data.environment_state_dos_path);
    appbox::sandbox->bEnvironmentComposed = inject_data.environment_is_composed;

    /*
     * The log of the process is written by the process itself into a file of
     * its own: the launcher is not part of the log path any more, so the
     * messages of two processes of one run cannot interleave and the tail of
     * the log survives a crash of the process which wrote it.
     */
    if (!inject_data.log_level.empty() && !appbox::SetLogLevelFromName(inject_data.log_level))
    {
        SPDLOG_WARN("the log level of the run is unknown: {}", inject_data.log_level);
    }

    if (!appbox::OpenLogFile(appbox::UTF8ToWide(inject_data.log_dir), appbox::GetImagePathFromPeb()))
    {
        SPDLOG_WARN("the log file of the process cannot be created in '{}'", inject_data.log_dir);
    }

    appbox::sandbox->client = std::make_shared<appbox::PipeClient>(appbox::sandbox->wPipePath);
    if (!appbox::sandbox->client->Start())
    {
        throw std::runtime_error("failed to start rpc client");
    }
}

static void LoadInjectData()
{
    const GUID guid = APPBOX_SANDBOX_GUID;
    DWORD      inject_bytes_sz = 0;
    void*      inject_bytes = nullptr;
    HMODULE    hModuleLast = nullptr;
    while ((hModuleLast = DetourEnumerateModules(hModuleLast)) != nullptr)
    {
        if ((inject_bytes = DetourFindPayload(hModuleLast, guid, &inject_bytes_sz)) != nullptr)
        {
            break;
        }
    }

    if (inject_bytes == nullptr)
    {
        SPDLOG_ERROR("failed to find inject data");
        throw std::runtime_error("failed to find inject data");
    }

    appbox::sandbox->inject_data = std::string(static_cast<const char*>(inject_bytes), inject_bytes_sz);
    ParseInjectData(appbox::sandbox->inject_data);
}

static void SayHello()
{
    LOG_I("AppBox Sandbox initialized for {} with config: {}", appbox::WideToUTF8(appbox::GetImagePathFromPeb()),
          appbox::DumpJson(nlohmann::json(*appbox::sandbox)));
}

/**
 * @brief Whether the module table was applied successfully.
 */
static bool s_modules_initialized = false;

/**
 * @brief RAII owner of the global sandbox instance.
 *
 * The instance is created by the constructor and released by the destructor
 * unless Release() was called, so a failure or an exception in the middle of
 * the initialization sequence never leaks it.
 */
class SandboxGuard
{
public:
    SandboxGuard()
    {
        appbox::sandbox = new appbox::Sandbox;
    }

    ~SandboxGuard()
    {
        if (!released_)
        {
            delete appbox::sandbox;
            appbox::sandbox = nullptr;
        }
    }

    SandboxGuard(const SandboxGuard&) = delete;
    SandboxGuard& operator=(const SandboxGuard&) = delete;
    SandboxGuard(SandboxGuard&&) = delete;
    SandboxGuard& operator=(SandboxGuard&&) = delete;

    /**
     * @brief Keep the sandbox instance alive after the guard is destroyed.
     */
    void Release()
    {
        released_ = true;
    }

private:
    bool released_ = false;
};

/**
 * @brief Handle the DLL_PROCESS_ATTACH notification.
 *
 * The sandbox instance and every module of the module table are released again
 * when the initialization fails, so the DLL can be unloaded cleanly.
 *
 * @return true when the sandbox was initialized, otherwise false.
 */
static bool OnDllAttach()
{
    if (DetourIsHelperProcess())
    {
        return true;
    }

    try
    {
        SandboxGuard guard;

        appbox::sandbox->bIsolationMode = DetourRestoreAfterWith();
        if (appbox::sandbox->bIsolationMode)
        {
            LoadInjectData();

            /*
             * The report of a crash is installed once the log file of the
             * process is open and before the hooks are attached: it collects
             * the modules through the original entry points of the process and
             * it covers the whole life of the process, including the attach.
             */
            appbox::InstallCrashHandler();
        }

        if (!appbox::InitModuleTable(s_module, std::size(s_module)))
        {
            SPDLOG_ERROR("failed to initialize sandbox modules");
            /* InitModuleTable() already rolled back the initialized modules. */
            return false;
        }

        s_modules_initialized = true;

        /*
         * The instance has to stay alive as long as the module table is
         * initialized, because the deinitialization of the hooks uses it.
         */
        guard.Release();

        /* Reporting the configuration must not fail the whole attach. */
        SayHello();
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("Sandbox attach error: {}", e.what());
        return false;
    }

    return true;
}

/**
 * @brief Handle the DLL_PROCESS_DETACH notification.
 *
 * @param[in] process_terminating Whether the notification comes from the exit
 *                                sequence of the process instead of a
 *                                `FreeLibrary` call.
 */
static void OnDllDetach(bool process_terminating)
{
    /*
     * Nothing of the sandbox is torn down while the process is terminating.
     *
     * The kernel reclaims every resource of a process which exits, so the
     * teardown adds no value and it adds risk: detaching the hooks patches
     * code which the threads the process still runs may be executing, and
     * releasing the objects the sandbox owns — the mounted hive, the
     * environment blocks it handed out, the handle table — can be observed by
     * the runtime of the application, which releases its own references to
     * them while it exits. A process which is killed by such a step during its
     * exit sequence dies with an access violation and reports no reason,
     * because the log of the exit sequence is gone as well.
     *
     * The log is switched off, so the exit sequence neither writes nor waits
     * for anything the sandbox owns. The file itself stays open: the kernel
     * closes it, and everything which was written before is already in it.
     *
     * The runtime library of the module stays initialized for the same reason:
     * see the entry point below, which skips its uninitialization while the
     * process is terminating.
     */
    appbox::LogEnable(false);

    if (process_terminating)
    {
        return;
    }

    /* The log sink refers to the log file, release it first. */
    appbox::CloseLogFile();

    if (s_modules_initialized)
    {
        /* Deinitialize in reverse order. */
        appbox::ExitModuleTable(s_module, std::size(s_module));
        s_modules_initialized = false;
    }

    if (appbox::sandbox != nullptr)
    {
        delete appbox::sandbox;
        appbox::sandbox = nullptr;
    }
}

void appbox::to_json(nlohmann::json& j, const Sandbox& r)
{
    j["bIsolationMode"] = r.bIsolationMode;
    j["wPipePath"] = appbox::WideToUTF8(r.wPipePath);
    j["fs"] = r.fs;
    j["variables"] = r.variables.size();
    j["sandbox32_dos_path"] = r.sandbox32_dos_path;
    j["sandbox64_dos_path"] = r.sandbox64_dos_path;
    j["registry_hive_dos_path"] = appbox::WideToUTF8(r.wRegistryHiveDOSPath);
    j["registry_isolation_dos_paths"] = r.wRegistryIsolationDOSPaths.size();
    j["filesystem_isolation_dos_paths"] = r.wFilesystemIsolationDOSPaths.size();
    j["fs_isolation_entries"] = r.fs_isolation.Count();
    j["font_files"] = r.wFontPaths.size();
    j["network_isolation_dos_paths"] = r.wNetworkIsolationDOSPaths.size();
    j["dns_entries"] = r.dns_table.Count();
    j["environment_isolation_dos_paths"] = r.wEnvironmentIsolationDOSPaths.size();
    j["environment_state_dos_path"] = appbox::WideToUTF8(r.wEnvironmentStateDOSPath);
    j["environment_variables"] = r.env_table.Count();
    j["environment_modifications"] = r.env_state.Count();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    try
    {
        switch (fdwReason)
        {
        case DLL_PROCESS_ATTACH:
            return OnDllAttach() ? TRUE : FALSE;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            OnDllDetach(lpvReserved != nullptr);
            break;
        default:
            throw std::runtime_error("unknown fdwReason");
        }
    }
    catch (std::runtime_error& e)
    {
        SPDLOG_ERROR("Sandbox DllMain error: {}", e.what());
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Entry point the runtime library of the module provides.
 *
 * @param[in] instance Handle of the module.
 * @param[in] reason Reason of the notification.
 * @param[in] reserved Whether the module is unloaded while the process
 *                     terminates.
 * @return Whether the notification was handled.
 */
extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE instance, DWORD reason, LPVOID reserved);

/**
 * @brief Entry point of the module.
 *
 * The runtime library of a module which links it statically belongs to that
 * module: its detach notification destroys the static objects of the module and
 * releases the heap, the locks and the thread data of the runtime library. The
 * toolset notes the difference in `dll_dllmain.cpp`: the runtime library of a
 * module which links it dynamically is uninitialized by the DLL which owns it,
 * a DLL which outlives the module.
 *
 * The hooks of the sandbox are still called while the process exits: the detach
 * notification of this module is the first of the exit sequence — the module is
 * loaded last — and the modules which are detached after it call the entry
 * points the sandbox hooks, which the end-to-end cases observe as an exit
 * sequence which fails inside the sandbox (`0xC0000409`, the `abort()` of the
 * unreachable sentinel of the log path). The runtime library therefore has to
 * stay usable for the whole life of the process, exactly like the one of the
 * dynamic form does.
 *
 * The entry point runs the detach of the sandbox itself and skips the
 * uninitialization of the runtime library while the process is terminating. The
 * kernel reclaims the state of the module either way, the log of the run is
 * switched off by OnDllDetach(), and the hooks keep isolating the application
 * until it is gone. A module which `FreeLibrary` unloads keeps the complete
 * teardown, the hooks included.
 *
 * @param[in] instance Handle of the module.
 * @param[in] reason Reason of the notification.
 * @param[in] reserved Whether the process is terminating.
 * @return Whether the notification was handled.
 */
extern "C" BOOL WINAPI AppBoxSandboxEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr)
    {
        OnDllDetach(true);
        return TRUE;
    }

    return _DllMainCRTStartup(instance, reason, reserved);
}
