#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "hook/__init__.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtCurrentTeb.hpp"
#include "utils/Defines.hpp"
#include "utils/HandleInfo.hpp"
#include "utils/Log.hpp"
#include "ModuleTable.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"

static const appbox::ModuleInitializer s_module[] = {
    { appbox::HandleInfo::Init, appbox::HandleInfo::Exit },
    { appbox::InitHook,         appbox::ExitHook         },
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

    appbox::sandbox->client = std::make_shared<appbox::PipeClient>(appbox::sandbox->wPipePath);
    if (!appbox::sandbox->client->Start())
    {
        throw std::runtime_error("failed to start rpc client");
    }

    /* Forward every log message to the loader over the RPC pipe. */
    appbox::SetLogSink([](const appbox::MsgLog::Req& req, nlohmann::json& rsp) {
        if (appbox::sandbox == nullptr || appbox::sandbox->client == nullptr)
        {
            return false;
        }

        return appbox::sandbox->client->Call(appbox::MsgLog::Method, req, rsp);
    });
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

static std::string GetImagePathFromPeb()
{
    auto         peb = sys_NtCurrentTeb()->ProcessEnvironmentBlock;
    auto&        path = peb->ProcessParameters->ImagePathName;
    std::wstring name(path.Buffer, path.Length / sizeof(wchar_t));
    return appbox::WideToUTF8(name);
}

static void SayHello()
{
    LOG_I("AppBox Sandbox initialized for {} with config: {}", GetImagePathFromPeb(),
          nlohmann::json(*appbox::sandbox).dump());
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
 */
static void OnDllDetach()
{
    /* The log sink refers to the sandbox instance, uninstall it first. */
    appbox::SetLogSink(nullptr);

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
    j["sandbox32_dos_path"] = r.sandbox32_dos_path;
    j["sandbox64_dos_path"] = r.sandbox64_dos_path;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
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
            OnDllDetach();
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
