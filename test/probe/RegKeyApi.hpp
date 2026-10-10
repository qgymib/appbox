#ifndef APPBOX_TEST_PROBE_REGKEYAPI_HPP
#define APPBOX_TEST_PROBE_REGKEYAPI_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

namespace appbox::test
{

/**
 * @brief Last write time which the probe writes into a key.
 *
 * 2001-01-01T00:00:00Z in FILETIME ticks. The time is far away from the time a
 * key carries which the test created just before the probe runs, so a test can
 * tell the time of the sandbox from the time of the host.
 */
constexpr uint64_t kProbeWriteTime = 126227808000000000ULL;

/**
 * @brief Protocol of the remaining key API probe.
 *
 * The probe calls one of the key entry points the registry isolation hooks on a
 * handle of the layer the mode selects and reports the status of the call
 * together with the last write time of the key as the sandboxed process sees
 * it, so a test can compare the sandbox with the state of the real registry.
 */
struct ProtocolRegKeyApi
{
    struct Req
    {
        std::string Key;  /* Key path relative to HKCU. Encoding in UTF-8. */
        std::string Api;  /* `NtSetInformationKey`, `NtFlushKey`, `NtCompressKey` or `NtLockRegistryKey`. */
        std::string Mode; /* `hive_handle` opens the key for writing, which the isolation answers with a
                           * handle of the sandbox hive; `read_handle` opens it read only, which the
                           * isolation answers with a handle of the host layer (the read through). */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Key, Api, Mode)
    };

    struct Rsp
    {
        DWORD    open_code = static_cast<DWORD>(-1);  /* Open of the handle the call runs on. */
        DWORD    api_code = static_cast<DWORD>(-1);   /* NTSTATUS of the call. */
        DWORD    query_code = static_cast<DWORD>(-1); /* Read of the last write time after the call. */
        uint64_t write_time_before = 0;               /* LastWriteTime before the call, in FILETIME ticks. */
        uint64_t write_time_after = 0;                /* LastWriteTime after the call, in FILETIME ticks. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, api_code, query_code, write_time_before,
                                                    write_time_after)
    };
};

/**
 * @brief Remaining key API probe: call a key entry point of the isolation on a
 *        handle of a selected layer and report the answer.
 *
 * `NtSetInformationKey(KeyWriteTimeInformation)` is the only call of the probe
 * which changes the key: it writes the fixed time of `kProbeWriteTime`, so the
 * caller of the probe can tell a key of the sandbox from a key of the host.
 */
extern Probe ProbeRegKeyApi;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGKEYAPI_HPP
