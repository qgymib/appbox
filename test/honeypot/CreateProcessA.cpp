#include "utils/winapi.hpp"
#include "utils/macros.hpp"
#include "utils/wstring.hpp"
#include "__init__.hpp"
#include <spdlog/spdlog.h>

static void s_test_CreateProcessA()
{
    static wchar_t path[4096];
    GetModuleFileNameW(nullptr, path, ARRAY_SIZE(path));

    static char args[4096];
    snprintf(args, sizeof(args), "%s", "-h");

    STARTUPINFOA startupInfo;
    ZeroMemory(&startupInfo, sizeof(STARTUPINFOA));
    startupInfo.cb = sizeof(STARTUPINFOA);

    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));

    std::string pathAnsi = appbox::wcstombs(path, CP_ACP);
    BOOL result = ::CreateProcessA(pathAnsi.c_str(), args, nullptr, nullptr, FALSE, 0, nullptr,
                                   nullptr, &startupInfo, &processInfo);
    if (!result)
    {
        throw std::runtime_error("CreateProcessA() failed");
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
}

const appbox::TestCase appbox::CreateProcessA = {
    L"CreateProcessA",
    s_test_CreateProcessA,
};
