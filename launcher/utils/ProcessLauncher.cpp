#include "ProcessLauncher.hpp"
#include "BuildCommandLine.hpp"

DWORD appbox::ProcessLauncher(const std::wstring& path, const std::vector<std::wstring> args, bool hide_console)
{
    auto cmd = appbox::BuildCommandLine(path, args);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    /*
     * A console program started by a program without a console gets a console
     * window of its own. The flag creates that console without the window, so
     * nothing appears on the desktop; the program keeps its console and
     * therefore its standard streams. A GUI program never owns a console
     * window and is not affected by the flag.
     */
    const DWORD creation_flags = hide_console ? CREATE_NO_WINDOW : 0;

    if (!CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, creation_flags, nullptr, nullptr, &si, &pi))
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
