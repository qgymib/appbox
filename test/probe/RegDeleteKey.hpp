#ifndef APPBOX_TEST_PROBE_REGDELETEKEY_HPP
#define APPBOX_TEST_PROBE_REGDELETEKEY_HPP

#include "WinAPI.h" /* Must be first include file */
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
    /**
     * @brief Entry point the probe deletes through.
     *
     * - `Reg` - `RegDeleteKeyW`, which opens the key with the right to delete,
     *   a write access which the isolation copies up into the hive for
     *   `WriteCopy`. This is the default.
     * - `ReadHandle` - `NtDeleteKey` on a handle which was opened read only,
     *   which the read through answers with a handle of the real registry.
     */
    enum class ModeKind
    {
        Reg,       ///< `reg`, deletes through `RegDeleteKeyW`.
        ReadHandle ///< `read_handle`, deletes through `NtDeleteKey`.
    };

    struct Req
    {
        std::string Root;                 /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Key;                  /* Key path relative to the root key. Encoding in UTF-8. */
        ModeKind    Mode = ModeKind::Reg; /* Entry point of the delete, see ModeKind. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Mode)
    };

    struct Rsp
    {
        DWORD       open_code = static_cast<DWORD>(-1);   /* Open of the handle the delete runs on. */
        DWORD       delete_code = static_cast<DWORD>(-1); /* The delete call itself. */
        DWORD       reopen_code = static_cast<DWORD>(-1); /* Read access open after the delete. */
        DWORD       query_code = static_cast<DWORD>(-1);  /* Read of the value `HostValue` after the delete. */
        std::string readback;                             /* Data of `HostValue`. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, delete_code, reopen_code, query_code, readback)
    };
};

/**
 * @brief The tokens of ModeKind, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token deletes through `RegDeleteKeyW`.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolRegDeleteKey::ModeKind,
                             {
                                 { ProtocolRegDeleteKey::ModeKind::Reg,        "reg"         },
                                 { ProtocolRegDeleteKey::ModeKind::ReadHandle, "read_handle" },
})

/**
 * @brief Delete key probe: delete a key of the view and report the view after
 *        the call.
 *
 * A `ReadHandle` request calls `NtDeleteKey` on a handle of the real registry:
 * the call must not reach the host, and the delete code of the kernel is
 * reported so a test can pin the behaviour.
 */
extern Probe ProbeRegDeleteKey;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGDELETEKEY_HPP
