#ifndef APPBOX_TEST_PROBE_REGTRANSACTED_HPP
#define APPBOX_TEST_PROBE_REGTRANSACTED_HPP

#include "WinAPI.h" /* Must be first include file */
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

/**
 * @brief Protocol of the transacted key probe.
 *
 * The probe calls one of the transacted registry entry points inside the
 * sandbox and reports every step of the call: the mount of a hive of its own,
 * the creation of the transaction, the transacted call itself, the value it
 * writes through the returned handle, the read back inside the transaction, the
 * end of the transaction and the state of the view afterwards. A test can
 * therefore pin the transaction of the caller and the isolation at once.
 *
 * The entry points have no Win32 wrapper which a test could use, so the probe
 * resolves `NtCreateKeyTransacted`, `NtOpenKeyTransacted` and
 * `NtOpenKeyTransactedEx` from `ntdll` and the transaction API from `ktmw32`.
 *
 * The probe addresses a key of the view with its **full NT path** instead of a
 * root handle: the predefined handles of the Win32 API (`HKEY_CURRENT_USER`,
 * ...) are not accepted by the NT entry points, which report
 * `STATUS_INVALID_HANDLE` for them, while the full path form is what the hooks
 * of the isolation resolve as well.
 */
struct ProtocolRegTransacted
{
    static constexpr const char* Method = "RegTransacted";

    /**
     * @brief Transacted entry point the probe calls.
     *
     * - `Create` - `NtCreateKeyTransacted`. This is the default.
     * - `Open` - `NtOpenKeyTransacted`.
     * - `OpenEx` - `NtOpenKeyTransactedEx`.
     */
    enum class ApiKind
    {
        Create, ///< `create`, runs `NtCreateKeyTransacted`.
        Open,   ///< `open`, runs `NtOpenKeyTransacted`.
        OpenEx  ///< `open_ex`, runs `NtOpenKeyTransactedEx`.
    };

    /**
     * @brief Access the key is opened with.
     *
     * - `Write` - `KEY_ALL_ACCESS`. This is the default.
     * - `Read` - `KEY_READ`.
     */
    enum class AccessKind
    {
        Write, ///< `write`, opens with `KEY_ALL_ACCESS`.
        Read   ///< `read`, opens with `KEY_READ`.
    };

    /**
     * @brief How the transaction ends.
     *
     * - `Close` - the handle of the transaction is closed, which commits it as
     *   well because it is the last handle. This is the default.
     * - `Commit` - `CommitTransaction`.
     * - `Rollback` - `RollbackTransaction`.
     */
    enum class EndKind
    {
        Close,   ///< `close`, closes the handle, which commits the transaction.
        Commit,  ///< `commit`, commits the transaction.
        Rollback ///< `rollback`, rolls the transaction back.
    };

    struct Req
    {
        std::string Root;                       /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Hive;                       /* DOS path of an application hive the probe mounts itself, empty
                                                 * for a root key of the view. A hive which the probe mounts is a
                                                 * path outside the view, so the transacted call names an object the
                                                 * isolation does not redirect. */
        std::string Key;                        /* Key path relative to the root key, or to the mounted hive. */
        ApiKind     Api = ApiKind::Create;      /* Entry point of the call, see ApiKind. */
        AccessKind  Access = AccessKind::Write; /* Access of the open, see AccessKind. */
        std::string Value;                      /* Name of the value the probe writes through the handle, empty
                                                 * for no write. */
        std::string Data;                       /* Data of the value, written as REG_SZ. */
        EndKind     End = EndKind::Close;       /* End of the transaction, see EndKind. */
        bool        ReadBack = true;            /* Read the value back inside the transaction. */
        bool        ReadAfter = true;           /* Read the value again after the transaction ended. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Hive, Key, Api, Access, Value, Data, End, ReadBack,
                                                    ReadAfter)
    };

    struct Rsp
    {
        DWORD mount_code = static_cast<DWORD>(-1);            /* RegLoadAppKeyW of the hive of the request, or
                                                               * the resolution of the path of the key. */
        DWORD       tx_code = static_cast<DWORD>(-1);         /* CreateTransaction() of the probe. */
        DWORD       call_code = static_cast<DWORD>(-1);       /* NTSTATUS of the transacted entry point. */
        DWORD       disposition = static_cast<DWORD>(-1);     /* Disposition of a transacted create. */
        DWORD       write_code = static_cast<DWORD>(-1);      /* Write of the value through the handle. */
        DWORD       read_code = static_cast<DWORD>(-1);       /* Read of the value inside the transaction. */
        std::string readback;                                 /* The value as the transaction reads it. */
        DWORD       end_code = static_cast<DWORD>(-1);        /* Commit, rollback or close of the transaction. */
        DWORD       after_open_code = static_cast<DWORD>(-1); /* Open of the key after the transaction ended. */
        DWORD       after_read_code = static_cast<DWORD>(-1); /* Read of the value after the transaction ended. */
        std::string after;                                    /* The value as the view reads it afterwards. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, mount_code, tx_code, call_code, disposition, write_code,
                                                    read_code, readback, end_code, after_open_code, after_read_code,
                                                    after)
    };
};

/**
 * @brief The tokens of ApiKind, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token creates the key.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolRegTransacted::ApiKind, {
                                                                 { ProtocolRegTransacted::ApiKind::Create, "create"  },
                                                                 { ProtocolRegTransacted::ApiKind::Open,   "open"    },
                                                                 { ProtocolRegTransacted::ApiKind::OpenEx, "open_ex" },
})

/**
 * @brief The tokens of AccessKind, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token opens the key with `KEY_ALL_ACCESS`.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolRegTransacted::AccessKind,
                             {
                                 { ProtocolRegTransacted::AccessKind::Write, "write" },
                                 { ProtocolRegTransacted::AccessKind::Read,  "read"  },
})

/**
 * @brief The tokens of EndKind, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token closes the handle, which commits the transaction.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolRegTransacted::EndKind,
                             {
                                 { ProtocolRegTransacted::EndKind::Close,    "close"    },
                                 { ProtocolRegTransacted::EndKind::Commit,   "commit"   },
                                 { ProtocolRegTransacted::EndKind::Rollback, "rollback" },
})

/**
 * @brief Transacted key probe: run a transacted open or create inside the
 *        sandbox and report the state of the view around it.
 *
 * The transaction is created by the probe, so a test can tell a call which the
 * isolation answered from a call which reached the real registry: the host
 * registry of a case never receives the key, the value or the rollback.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolRegTransacted::Rsp ProbeRegTransacted_Entry(const ProtocolRegTransacted::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGTRANSACTED_HPP
