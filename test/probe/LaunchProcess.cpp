#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "LaunchProcess.hpp"
#include "WString.hpp"
#include <vector>

static nlohmann::json ProbeLaunchProcess_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolLaunchProcess::Req>();

    appbox::test::ProtocolLaunchProcess::Rsp rsp;

    /* Build the command line: "<exe>" <arg> <arg> ... The arguments are
     * joined with plain spaces, quoting stays under the caller's control. */
    std::wstring cmdline = L"\"" + appbox::UTF8ToWide(req.FileName) + L"\"";
    for (const auto& arg : req.Arguments)
    {
        cmdline += L" ";
        cmdline += appbox::UTF8ToWide(arg);
    }
    std::vector<wchar_t> buffer(cmdline.begin(), cmdline.end());
    buffer.push_back(L'\0');

    STARTUPINFOW        startup_info;
    PROCESS_INFORMATION process_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    ZeroMemory(&process_info, sizeof(process_info));

    if (!CreateProcessW(appbox::UTF8ToWide(req.FileName).c_str(), buffer.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup_info, &process_info))
    {
        rsp.code = GetLastError();
        return rsp;
    }

    CloseHandle(process_info.hThread);

    if (WaitForSingleObject(process_info.hProcess, INFINITE) != WAIT_OBJECT_0)
    {
        rsp.code = GetLastError();
        CloseHandle(process_info.hProcess);
        return rsp;
    }

    if (!GetExitCodeProcess(process_info.hProcess, &rsp.exit_code))
    {
        rsp.code = GetLastError();
    }
    CloseHandle(process_info.hProcess);

    return rsp;
}

appbox::test::Probe appbox::test::ProbeLaunchProcess("LaunchProcess", ProbeLaunchProcess_Entry);
