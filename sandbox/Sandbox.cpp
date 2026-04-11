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
#include "WString.hpp"

appbox::Sandbox::Ptr appbox::sandbox;

static appbox::InjectData LoadInjectData()
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
    return nlohmann::json::parse(inject_string);
}

appbox::Sandbox::Ptr appbox::Sandbox::Create(HINSTANCE hinstDLL)
{
    auto obj = std::make_shared<appbox::Sandbox>();
    obj->hinstDLL = hinstDLL;

    auto inject_data = LoadInjectData();
    obj->pipe_path = inject_data.pipe_path;
    obj->sandbox_path = appbox::UTF8ToWide(inject_data.sandbox_path.c_str());
    obj->sandbox32_dll_path = inject_data.sandbox32_dll_path;
    obj->sandbox64_dll_path = inject_data.sandbox64_dll_path;

    obj->task_queue = appbox::TaskQueue::Create();
    return obj;
}

static void OnDllAttach(HINSTANCE hinstDLL)
{
    if (DetourIsHelperProcess())
    {
        return;
    }
    DetourRestoreAfterWith();

    appbox::sandbox = appbox::Sandbox::Create(hinstDLL);

    appbox::sandbox->client = appbox::AsyncInstance<appbox::RemoteClient>::Create([]() {
        auto c = appbox::RemoteClient::Create(appbox::sandbox->pipe_path);
        c->Start();
        return c;
    });

    appbox::InitHook();
    appbox::RemoteLogInit();
}

static void OnDllDetach()
{
    appbox::sandbox.reset();
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
