#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <detours.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "hook/__init__.hpp"
#include "utils/RemoteLog.hpp"
#include "Sandbox.hpp"
#include "Defines.hpp"

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
    appbox::sandbox->client = appbox::AsyncInstance<appbox::RemoteClient>::Create([]() {
        auto client = appbox::RemoteClient::Create(appbox::sandbox->inject_data.pipe_path);
        client->Start();
        return client;
    });

    appbox::InitHook();
    appbox::RemoteLogInit();
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
