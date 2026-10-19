#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <tlhelp32.h>
#include <cstdint>
#include <string>
#include <vector>
#include "WString.hpp"
#include "StartupStarted.hpp"

namespace
{

/**
 * @brief Get the id of the process which created this one.
 *
 * The snapshot of the system lists the parent of every process, which is what
 * a case needs to tell whether the launcher created the process itself or
 * whether it started a program of its own in between.
 *
 * @return The parent process id, zero when the snapshot cannot be read.
 */
std::uint32_t ParentProcessId()
{
    const DWORD self = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    std::uint32_t parent = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == self)
            {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return parent;
}

/**
 * @brief Get the path of the image this process runs.
 *
 * The path is the one the file system reports for the image, which is the file
 * of the layer the sandbox started the process from.
 *
 * @return The path in UTF-8, empty when it cannot be read.
 */
std::string ModulePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }

        if (length < buffer.size())
        {
            return appbox::WideToUTF8(std::wstring(buffer.data(), length));
        }

        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

static nlohmann::json ProbeStartupStarted_Entry(const nlohmann::json&)
{
    appbox::test::ProtocolStartupStarted::Rsp rsp;
    rsp.marker = appbox::test::StartupMarker();
    rsp.pid = GetCurrentProcessId();
    rsp.parent_pid = ParentProcessId();
    rsp.module_path = ModulePath();
    return rsp;
}

appbox::test::Probe appbox::test::ProbeStartupStarted("StartupStarted", ProbeStartupStarted_Entry);
