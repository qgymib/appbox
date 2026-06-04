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
#include "Sandbox.hpp"
#include "WString.hpp"

struct ModuleInitializer
{
    /**
     * @brief Initialize function.
     */
    NTSTATUS (*fn_init)();

    /**
     * @brief Exit function.
     */
    void (*fn_exit)();
};

static const ModuleInitializer s_module[] = {
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

static void OnDllAttach()
{
    if (DetourIsHelperProcess())
    {
        return;
    }

    appbox::sandbox = new appbox::Sandbox;
    appbox::sandbox->bIsolationMode = DetourRestoreAfterWith();
    if (appbox::sandbox->bIsolationMode)
    {
        LoadInjectData();
    }

    for (size_t i = 0; i < std::size(s_module); ++i)
    {
        auto st = s_module[i].fn_init();
        if (!NT_SUCCESS(st))
        {
            while (i > 0)
            {
                s_module[i - 1].fn_exit();
            }
            return;
        }
    }

    SayHello();
}

static void OnDllDetach()
{
    /* Deinitialize in reverse order. */
    for (auto i = std::size(s_module); i > 0; --i)
    {
        s_module[i - 1].fn_exit();
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
            OnDllAttach();
            break;
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
