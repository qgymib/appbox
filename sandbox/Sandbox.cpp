#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <detours.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "Sandbox.hpp"
#include "Defines.hpp"

appbox::Sandbox* appbox::g_sandbox = nullptr;

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
    appbox::g_sandbox->inject_data = nlohmann::json::parse(inject_string);
}

static void OnDllAttach(HINSTANCE hinstDLL)
{
    if (DetourIsHelperProcess())
    {
        return;
    }
    DetourRestoreAfterWith();

    appbox::g_sandbox = new appbox::Sandbox();
    appbox::g_sandbox->hinstDLL = hinstDLL;
    LoadInjectData();
}

static void OnDllDetach()
{
    if (appbox::g_sandbox != nullptr)
    {
        delete appbox::g_sandbox;
        appbox::g_sandbox = nullptr;
    }
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
