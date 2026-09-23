#ifndef APPBOX_TEST_PROBE_REGDELETEKEY_HPP
#define APPBOX_TEST_PROBE_REGDELETEKEY_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

/**
 * @brief Protocol of the delete key probe.
 *
 * The probe deletes a key of the sandbox view and reports what the view shows
 * afterwards, so a test can compare it with the state of the real registry.
 */
struct ProtocolRegDeleteKey
{
    struct Req
    {
        std::string Root; /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Key;  /* Key path relative to the root key. Encoding in UTF-8. */
        std::string Mode; /* `reg` deletes through RegDeleteKeyW, `read_handle` through NtDeleteKey. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Mode)
    };

    struct Rsp
    {
        DWORD open_code = static_cast<DWORD>(-1);   /* Open of the handle the delete runs on. */
        DWORD delete_code = static_cast<DWORD>(-1); /* The delete call itself. */
        DWORD reopen_code = static_cast<DWORD>(-1); /* Read access open after the delete. */
        DWORD query_code = static_cast<DWORD>(-1);  /* Read of the value `HostValue` after the delete. */
        std::string readback;                       /* Data of `HostValue`. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, delete_code, reopen_code, query_code, readback)
    };
};

/**
 * @brief Delete key probe: delete a key of the view and report the view after
 *        the call.
 *
 * The `reg` mode deletes through `RegDeleteKeyW`, which opens the key with the
 * right to delete — a write access which the isolation copies up into the hive
 * for `WriteCopy`. The `read_handle` mode opens the key read only (which the
 * read through answers with a handle of the real registry) and calls
 * `NtDeleteKey` on that handle: the call must not reach the real registry, and
 * the delete code of the kernel is reported so a test can pin the behaviour.
 */
extern Probe ProbeRegDeleteKey;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGDELETEKEY_HPP
