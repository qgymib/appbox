#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <tlhelp32.h>
#include "LoadedModules.hpp"
#include "WString.hpp"

static nlohmann::json ProbeLoadedModules_Entry(const nlohmann::json&)
{
    appbox::test::ProtocolLoadedModules::Rsp rsp;

    /*
     * The snapshot reports the modules the loader mapped into this process,
     * which is the list the case is about. It is taken from the process itself,
     * so it needs no handle to another process.
     */
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ::GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        if (::Module32FirstW(snapshot, &entry))
        {
            do
            {
                rsp.modules.push_back(appbox::WideToUTF8(entry.szModule));
            } while (::Module32NextW(snapshot, &entry));
        }

        ::CloseHandle(snapshot);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeLoadedModules("LoadedModules", ProbeLoadedModules_Entry);
