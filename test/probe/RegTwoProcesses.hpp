#ifndef APPBOX_TEST_PROBE_REGTWOPROCESSES_HPP
#define APPBOX_TEST_PROBE_REGTWOPROCESSES_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

/**
 * @brief Name of the value the worker writes with the text it read as the value
 *        of the parent.
 *
 * The parent reads that value back, so the report of the case describes what
 * the child process saw — which is the observation the exit code of the worker
 * carries as well (see RegChildExitCode).
 */
constexpr const char* kRegChildObservationValue = "ChildSawParent";

/**
 * @brief Protocol of the case which runs two processes of one sandbox.
 *
 * The probe process writes a value into a key of the sandbox registry, starts
 * the worker of `RegChildProcess.hpp` as a child process of its own, reads the
 * value of the child while the child is still alive and waits for it. Every
 * observation of the run is reported in one response, so a case asserts the
 * behaviour of the two processes with a single call:
 *
 * * the child saw the value of the parent (`child_exit_code`,
 *   `child_saw_readback`),
 * * the parent saw the value of the child (`child_value_readback`,
 *   `child_alive_when_read`),
 * * both processes mounted the hive of the sandbox (the codes of the calls).
 */
struct ProtocolRegTwoProcesses
{

    struct Req
    {
        std::string Root;               /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Key;                /* Key path relative to the root key. Encoding in UTF-8. */
        std::string ParentValue;        /* Name of the value the parent process writes. */
        std::string ParentData;         /* Text the parent process writes. */
        std::string ChildValue;         /* Name of the value the child process writes. */
        std::string ChildData;          /* Text the child process writes. */
        DWORD       ChildHoldMs = 2000; /* Time the child stays alive after its writes. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, ParentValue, ParentData, ChildValue, ChildData,
                                                    ChildHoldMs)
    };

    struct Rsp
    {
        std::string role;                                 /* Role the probe ran with, always "parent". */
        DWORD       create_code = static_cast<DWORD>(-1); /* RegCreateKeyExW() error code of the parent. */
        DWORD       disposition = 0;                      /* REG_CREATED_NEW_KEY / REG_OPENED_EXISTING_KEY. */
        DWORD       set_code = static_cast<DWORD>(-1);    /* RegSetValueExW() error code of the parent value. */
        std::string parent_readback;                      /* The value of the parent as it read it back. */
        DWORD       child_launch_code = 0;                /* CreateProcessW() error code, zero on success. */
        DWORD       child_exit_code = 0;                  /* Exit code of the worker process (see RegChildExitCode). */
        bool        child_timed_out = false;              /* The child did not leave within the timeout. */
        bool        child_alive_when_read = false;        /* The parent read the child value while the child ran. */
        DWORD       child_value_read_code = static_cast<DWORD>(-1); /* Parent read of the value of the child. */
        std::string child_value_readback;                           /* Value of the child as the parent read it. */
        DWORD       child_saw_read_code = static_cast<DWORD>(-1);   /* Parent read of the observation of the child. */
        std::string child_saw_readback;                             /* Text the child read as the parent value. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, role, create_code, disposition, set_code, parent_readback,
                                                    child_launch_code, child_exit_code, child_timed_out,
                                                    child_alive_when_read, child_value_read_code, child_value_readback,
                                                    child_saw_read_code, child_saw_readback)
    };
};

/**
 * @brief Registry probe which runs a child process of the sandbox: the parent
 *        writes a value, the child reads it and writes its own, the parent
 *        reads the value of the child while the child is alive.
 */
extern Probe ProbeRegTwoProcesses;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGTWOPROCESSES_HPP
