#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "hook/__init__.hpp"
#include "hook/NtCreateFile.hpp"
#include "utils/Log.hpp"
#include "Sandbox.hpp"
#include "Defines.hpp"

struct LogGuard
{
    LogGuard()
    {
        appbox::LogEnable(false);
    }

    ~LogGuard()
    {
        appbox::LogEnable(true);
    }
};

appbox::Sandbox* appbox::sandbox = nullptr;

static void LoadInjectData()
{
    const GUID guid = SANDBOX_GUID;
    DWORD      inject_data_sz = 0;
    void*      inject_data = nullptr;
    HMODULE    hModuleLast = nullptr;
    while ((hModuleLast = DetourEnumerateModules(hModuleLast)) != nullptr)
    {
        if ((inject_data = DetourFindPayload(hModuleLast, guid, &inject_data_sz)) != nullptr)
        {
            break;
        }
    }

    if (inject_data == nullptr)
    {
        SPDLOG_ERROR("failed to find inject data");
        throw std::runtime_error("failed to find inject data");
    }

    std::string inject_string(static_cast<const char*>(inject_data), inject_data_sz);
    appbox::sandbox->inject_data = nlohmann::json::parse(inject_string);
}

static void OnDllAttach(HINSTANCE hinstDLL)
{
    if (DetourIsHelperProcess())
    {
        return;
    }
    DetourRestoreAfterWith();

    appbox::sandbox = new appbox::Sandbox(hinstDLL);
    LoadInjectData();

    appbox::sandbox->task_queue = appbox::TaskQueue::Create();

    appbox::sandbox->client = std::make_shared<appbox::PipeClient>(appbox::sandbox->inject_data.pipe_path);
    if (!appbox::sandbox->client->Start())
    {
        throw std::runtime_error("failed to start rpc client");
    }

    appbox::InitHook();
    LOG_I("AppBox Sandbox initialized");
}

static void OnDllDetach()
{
    if (appbox::sandbox != nullptr)
    {
        delete appbox::sandbox;
        appbox::sandbox = nullptr;
    }
}

appbox::Sandbox::Sandbox(HINSTANCE hinstDLL)
{
    this->hinstDLL = hinstDLL;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    try
    {
        switch (fdwReason)
        {
        case DLL_PROCESS_ATTACH:
            OnDllAttach(hinstDLL);
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
