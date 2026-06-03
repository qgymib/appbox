#include "MiniLauncher.hpp"
#include "BuildCommandLine.hpp"

DWORD appbox::MiniLauncer(const std::wstring& path, const std::vector<std::wstring> args)
{
    auto cmd = appbox::BuildCommandLine(path, args);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        return GetLastError();
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    return exit_code;
}
