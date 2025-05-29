#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <stdexcept>
#include <detours.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "utils/file.hpp"
#include "utils/wstring.hpp"
#include "hooks/__init__.hpp"
#include "appbox.hpp"
#include "config.hpp"

appbox::AppBox* appbox::G = nullptr;

static nlohmann::json s_find_inject_data()
{
    const GUID guid = APPBOX_SANDBOX_GUID;
    DWORD      payload_sz = 0;
    void*      injectData = nullptr;
    HMODULE    hModuleLast = nullptr;
    while ((hModuleLast = DetourEnumerateModules(hModuleLast)) != nullptr)
    {
        if ((injectData = DetourFindPayload(hModuleLast, guid, &payload_sz)) != nullptr)
        {
            break;
        }
    }

    if (injectData == nullptr)
    {
        throw std::runtime_error("Failed to find inject data");
    }

    std::string injectString(static_cast<char*>(injectData), payload_sz);
    return nlohmann::json::parse(injectString);
}

static std::wstring s_get_current_process_path()
{
    std::array<wchar_t, 8192> buff;
    GetModuleFileNameW(nullptr, buff.data(), (DWORD)buff.size());
    return buff.data();
}

static std::wstring s_get_current_module_name()
{
    std::array<wchar_t, 8192> buff;
    GetModuleFileNameW(appbox::G->hinstDLL, buff.data(), (DWORD)buff.size());
    return buff.data();
}

static std::wstring s_get_log_file_path(const std::wstring& processName, DWORD pid)
{
    std::array<wchar_t, 8192> buff;
    _snwprintf_s(buff.data(), buff.size(), _TRUNCATE, L"%s-%u.txt", processName.c_str(), pid);

    return appbox::mbstowcs(appbox::G->inject.sandboxPath.c_str(), CP_UTF8) + L"\\" + buff.data();
}

static void s_setup_log()
{
    const std::wstring processPath = s_get_current_process_path();
    const std::wstring processName = appbox::BaseName(processPath);
    const std::wstring modulePath = s_get_current_module_name();
    const std::wstring moduleName = appbox::BaseName(modulePath);
    const std::string  moduleNameU8 = appbox::wcstombs(moduleName.c_str(), CP_UTF8);
    DWORD              pid = GetCurrentProcessId();
    const std::wstring logPath = s_get_log_file_path(processName, pid);

    auto logger = spdlog::basic_logger_mt(moduleNameU8, logPath);
    spdlog::set_default_logger(logger);

    spdlog::info(L"Inject={}, PID={}", processPath, pid);
}

static void s_on_process_attach(HINSTANCE hinstDLL)
{
    /*
     * When creating a 32-bit target process from a 64-bit parent process or creating a 64-bit
     * target process from a 32-bit parent process, a temporary helper process is created.
     *
     * When a user-supplied DLL is loaded into a helper process, it must not detour any functions.
     * Instead, it should perform no operations in DllMain.
     *
     * https://github.com/microsoft/Detours/wiki/DetourIsHelperProcess
     */
    if (DetourIsHelperProcess())
    {
        return;
    }

    appbox::G = new appbox::AppBox;
    appbox::G->hinstDLL = hinstDLL;
    appbox::G->inject = s_find_inject_data();
    s_setup_log();

    // TODO: inject filesystem and registry functions.
}

static void s_on_process_detach()
{
    if (appbox::G == nullptr)
    {
        return;
    }
    delete appbox::G;
    appbox::G = nullptr;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    try
    {
        switch (fdwReason)
        {
        case DLL_PROCESS_ATTACH:
            s_on_process_attach(hinstDLL);
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            s_on_process_detach();
            break;
        default:
            throw std::runtime_error("Unknown fdwReason");
        }
    }
    catch (std::runtime_error& e)
    {
        std::wstring msg = appbox::mbstowcs(e.what(), CP_UTF8);
        MessageBoxW(nullptr, msg.c_str(), L"AppBox Sandbox", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    // appbox::hook::Init();

    return TRUE;
}

appbox::AppBox::AppBox()
{
    hinstDLL = nullptr;
}
