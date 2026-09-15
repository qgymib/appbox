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

    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0)
    {
        const DWORD err = GetLastError();
        CloseHandle(pi.hProcess);
        return err;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &exit_code))
    {
        exit_code = GetLastError();
    }

    CloseHandle(pi.hProcess);
    return exit_code;
}
