#ifndef APPBOX_SANDBOX_REGISTRY_ISOLATIONPOLICY_HPP
#define APPBOX_SANDBOX_REGISTRY_ISOLATIONPOLICY_HPP

#include "WinAPI.h" /* Must be first include file */
#include "RegistryIsolation.hpp"
#include <optional>

namespace appbox
{
namespace registry
{

/**
 * @brief Whether an access mask may permit a modification of the key.
 *
 * The mask of an open is not a reliable statement about the calls which follow
 * it, so the test is deliberately conservative: every right which can modify a
 * key, a sub key or a value counts as a write, including `MAXIMUM_ALLOWED` and
 * the generic rights a caller may pass to the NT entry points. A read only mask
 * such as `KEY_READ` or `KEY_QUERY_VALUE` is the only case which is reported as
 * a read, which keeps the read through of the sandbox working.
 *
 * @param[in] access The access mask of the open.
 * @return true when the mask may permit a modification of the key.
 */
inline bool RequestsWrite(ACCESS_MASK access)
{
    constexpr ACCESS_MASK kWrite = KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK | DELETE | WRITE_DAC |
                                   WRITE_OWNER | MAXIMUM_ALLOWED | GENERIC_WRITE | GENERIC_ALL;
    return (access & kWrite) != 0;
}

/**
 * @brief Whether a status reports that the key does not exist.
 *
 * A missing key is reported as a missing object or as a missing parent of the
 * object, depending on how much of the path exists. Every other failure (an
 * access denial, for example) describes a key which is there but unusable, so
 * it must be preferred over the not found result of the other layer.
 *
 * @param[in] status The status code of a registry call.
 * @return true when the status reports that the key does not exist.
 */
inline bool IsKeyNotFound(NTSTATUS status)
{
    return status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND;
}

/**
 * @brief Pick the failure to report when both layers failed to open a key.
 *
 * A key which exists but is not accessible is not a missing key, so a failure
 * which is not a not-found result wins over one which is. When both layers
 * report a missing key the failure of the real registry is reported: it is the
 * error the caller sees without the isolation, which keeps the redirection
 * transparent for the caller.
 *
 * @param[in] real_status The status of the open against the real registry.
 * @param[in] hive_status The status of the open against the sandbox hive.
 * @return The status the hooked call reports.
 */
inline NTSTATUS PickOpenFailure(NTSTATUS real_status, NTSTATUS hive_status)
{
    if (IsKeyNotFound(real_status))
    {
        return IsKeyNotFound(hive_status) ? real_status : hive_status;
    }
    return real_status;
}

/**
 * @brief Whether the isolation mode of a key keeps the host entry invisible.
 *
 * `Full` and `Hide` both describe a key the sandboxed process may only see
 * through the virtual registry: a key which the hive does not hold is reported
 * as not found instead of falling back to the host. `Merge` and `WriteCopy`
 * keep the host entry visible.
 *
 * @param[in] mode The isolation mode of the key.
 * @return true when the host key must stay invisible.
 */
inline bool KeyHidesHost(RegistryKeyIsolation mode)
{
    return mode == RegistryKeyIsolation::Full || mode == RegistryKeyIsolation::Hide;
}

/**
 * @brief Whether the effective mode of a value keeps the host value invisible.
 *
 * A value has two modes of its own and both of them hide the host value:
 * `Full` shows the value of the virtual registry and `Hide` hides the value
 * altogether. A value which the isolation file does not list follows the mode
 * of the key which holds it, so a value below a `Merge` or `WriteCopy` key
 * shows the host value.
 *
 * @param[in] value_mode The listed mode of the value, empty when the file does
 *                       not list the value.
 * @param[in] key_mode The effective mode of the key which holds the value.
 * @return true when the host value must stay invisible.
 */
inline bool ValueHidesHost(std::optional<RegistryValueIsolation> value_mode, RegistryKeyIsolation key_mode)
{
    if (value_mode.has_value())
    {
        /* `Full` hides the host value and `Hide` hides the value itself. */
        return true;
    }

    return KeyHidesHost(key_mode);
}

/**
 * @brief Whether a modification of a value lands in the host registry.
 *
 * `Merge` is the only mode which lets a modification of a value reach the host
 * registry, and it does so by the rule of the mode: a value the sandbox holds
 * — the value of the hive or a value the sandbox deleted, which the whiteout
 * store keeps — is modified inside the sandbox, a value the sandbox does not
 * hold is modified in the real registry.
 *
 * Every other mode redirects the modification into the sandbox, which is what
 * keeps the host registry untouched outside `Merge`.
 *
 * @param[in] value_mode The listed mode of the value, empty when the file does
 *                       not list the value.
 * @param[in] key_mode The effective mode of the key which holds the value.
 * @param[in] sandbox_holds Whether the sandbox holds the value, the whiteout of
 *                          a deleted value included.
 * @return true when the modification has to be applied to the host registry.
 */
inline bool ValueWritesToHost(std::optional<RegistryValueIsolation> value_mode, RegistryKeyIsolation key_mode,
                              bool sandbox_holds)
{
    if (value_mode.has_value())
    {
        /* A value mode always redirects the writes of the value into the sandbox. */
        return false;
    }

    if (key_mode != RegistryKeyIsolation::Merge)
    {
        return false;
    }

    return !sandbox_holds;
}

/**
 * @brief What an isolated open does when the sandbox hive does not hold the key.
 *
 * The registry isolation redirects every key below one of the root keys of the
 * view into the private hive. The isolation mode of the key decides which host
 * entries stay visible and where a modification lands, so the open entry points
 * cannot answer every key the same way. The fallback below describes the second
 * half of that decision, the one which is taken when the hive layer does not
 * hold the key:
 *
 * | Mode | Read access | Write access |
 * | --- | --- | --- |
 * | `Full` | `ReportHiveFailure` | `ReportHiveFailure` |
 * | `Hide` | `ReportHiveFailure` | `ReportHiveFailure` |
 * | `Merge` | `UseHost` | `UseHost` |
 * | `WriteCopy` | `UseHost` | `CopyUp` |
 *
 * The rows of `Full` and `Hide` are the rule `KeyHidesHost()` describes: the
 * host entry of such a key must not be used. `Merge` hands the key of the host
 * layer out for a write access open as well, because a modification of a key
 * the sandbox does not hold lands in the host registry; `WriteCopy` copies the
 * key up instead, so the modification lands in the sandbox.
 */
enum class OpenFallback
{
    /**
     * @brief The host entry must not be used; the failure of the hive open is
     *        the result of the call.
     */
    ReportHiveFailure,

    /**
     * @brief The key is opened in the host registry.
     */
    UseHost,

    /**
     * @brief The key is copied up: a shadow key is created inside the hive and
     *        handed out, so every modification lands in the sandbox.
     */
    CopyUp,
};

/**
 * @brief The fallback of an isolated open whose hive layer does not hold the key.
 *
 * The rows of the table are described by OpenFallback; the rule of `Full` and
 * `Hide` agrees with `KeyHidesHost()`, so a key whose host entry is invisible
 * never reaches the host registry.
 *
 * @param[in] mode The isolation mode of the key.
 * @param[in] desired_access The access mask of the open.
 * @return The fallback the open has to run.
 */
inline OpenFallback FallbackForKey(RegistryKeyIsolation mode, ACCESS_MASK desired_access)
{
    switch (mode)
    {
    case RegistryKeyIsolation::Full:
    case RegistryKeyIsolation::Hide:
        /* The host entry is invisible for the sandbox. */
        return OpenFallback::ReportHiveFailure;
    case RegistryKeyIsolation::Merge:
        /* A modification of a key the sandbox does not hold lands in the host. */
        return OpenFallback::UseHost;
    case RegistryKeyIsolation::WriteCopy:
        /* The sandbox takes precedence: every modification lands in the hive. */
        return RequestsWrite(desired_access) ? OpenFallback::CopyUp : OpenFallback::UseHost;
    }
    return OpenFallback::ReportHiveFailure;
}

/**
 * @brief What a transacted open does when the sandbox hive does not hold the key.
 *
 * A transacted entry point (`NtOpenKeyTransacted`, `NtOpenKeyTransactedEx` and
 * `NtCreateKeyTransacted`) binds the key it returns to a transaction of the
 * caller, so every change the caller makes through that key is committed or
 * rolled back with the transaction. The isolation redirects such a call into
 * the sandbox hive, which the kernel enlists into the transaction of the
 * caller: the hive is the write layer of the sandbox, so the transaction of the
 * caller stays inside the sandbox.
 *
 * The host layer cannot take that route. A transacted open of a key of the real
 * registry enlists the hive of the host — the user hive, for example — into the
 * transaction of the sandboxed process, which is a change of the host the
 * isolation must not cause: the kernel writes the transaction log of the real
 * hive and the transaction of the caller can conflict with a transaction
 * another process holds on the same hive. The host layer is therefore never
 * used by a transacted call, whatever the isolation mode says about a plain
 * call.
 *
 * | Mode | Read access | Write access |
 * | --- | --- | --- |
 * | `Full` | `ReportHiveFailure` | `ReportHiveFailure` |
 * | `Hide` | `ReportHiveFailure` | `ReportHiveFailure` |
 * | `Merge` | `RefuseHostFallback` | `RefuseHostFallback` |
 * | `WriteCopy` | `RefuseHostFallback` | `CopyUp` |
 *
 * The rows of `Full` and `Hide` are the ones of `FallbackForKey`: the host entry
 * of such a key must not be used at all. `WriteCopy` copies a write access open
 * up into the hive — the shadow key is created with the transaction of the
 * caller, so a rollback removes it — while every call which would have to answer
 * with a handle of the host layer is refused.
 *
 * The hive of the sandbox is an application hive (`RegLoadAppKey`) and an
 * application hive does not support transactions: the kernel answers every
 * transacted operation on one with `STATUS_RM_NOT_ACTIVE`, which is the failure
 * the hive layer reports. The table below still describes the route, so a hive
 * which supports transactions is answered by the hive layer alone.
 */
enum class TransactedFallback
{
    /**
     * @brief The host entry must not be used; the failure of the hive open is
     *        the result of the call.
     */
    ReportHiveFailure,

    /**
     * @brief The key is copied up: a shadow key is created inside the hive with
     *        the transaction of the caller and handed out.
     */
    CopyUp,

    /**
     * @brief The host entry would be needed, which the isolation cannot enlist
     *        into the transaction of the caller: the call is refused.
     */
    RefuseHostFallback,
};

/**
 * @brief The status a transacted call reports when the isolation refuses it.
 *
 * The isolation answers a transacted call with a key of the hive, with the
 * failure of the hive layer or with this status, so a caller which observes it
 * knows that the key is not part of the view for a transacted open: the host
 * entry of the key would have to be enlisted into the transaction of the
 * caller, which the isolation does not do.
 */
inline constexpr NTSTATUS kTransactedHostFallbackStatus = STATUS_NOT_SUPPORTED;

/**
 * @brief The fallback of a transacted open whose hive layer does not hold the key.
 *
 * The rows of the table are described by TransactedFallback. The rule agrees
 * with `FallbackForKey()` except for the host layer: a transacted call never
 * answers with a handle of the host layer, because the host layer would be
 * enlisted into the transaction of the caller.
 *
 * @param[in] mode The isolation mode of the key.
 * @param[in] desired_access The access mask of the open.
 * @return The fallback the transacted open has to run.
 */
inline TransactedFallback FallbackForKeyTransacted(RegistryKeyIsolation mode, ACCESS_MASK desired_access)
{
    switch (mode)
    {
    case RegistryKeyIsolation::Full:
    case RegistryKeyIsolation::Hide:
        /* The host entry is invisible for the sandbox. */
        return TransactedFallback::ReportHiveFailure;
    case RegistryKeyIsolation::Merge:
        /* The key lives in the host, which the transaction must not enlist. */
        return TransactedFallback::RefuseHostFallback;
    case RegistryKeyIsolation::WriteCopy:
        /* A write is copied up into the hive; a read has no host layer to use. */
        return RequestsWrite(desired_access) ? TransactedFallback::CopyUp : TransactedFallback::RefuseHostFallback;
    }
    return TransactedFallback::ReportHiveFailure;
}

/**
 * @brief The disposition a create reports for the merged view.
 *
 * A create lands in the state hive of the sandbox, so the disposition of the
 * kernel describes that layer alone. The view of the sandboxed process is the
 * state hive merged with the image of the run, which is merged with the visible
 * entries of the host layer, so the value the caller observes has to be derived
 * from all of them:
 *
 * | Hive disposition | Image holds the key | Mode | Host holds the key | Reported disposition |
 * | --- | --- | --- | --- | --- |
 * | `REG_OPENED_EXISTING_KEY` | not consulted | every mode | not consulted | `REG_OPENED_EXISTING_KEY` |
 * | `REG_CREATED_NEW_KEY` | yes | every mode | not consulted | `REG_OPENED_EXISTING_KEY` |
 * | `REG_CREATED_NEW_KEY` | no | `Full`, `Hide` | not consulted | `REG_CREATED_NEW_KEY` |
 * | `REG_CREATED_NEW_KEY` | no | `Merge`, `WriteCopy` | yes | `REG_OPENED_EXISTING_KEY` |
 * | `REG_CREATED_NEW_KEY` | no | `Merge`, `WriteCopy` | no | `REG_CREATED_NEW_KEY` |
 *
 * A key which the state hive already holds exists in the view of every mode, so
 * its disposition is reported unchanged. A key the image holds exists in the
 * view of every mode as well, because the image is a sandbox layer: `Full` and
 * `Hide` keep the host registry invisible and never the layers of the sandbox.
 * A key which only the host holds does not exist in the view of `Full` and
 * `Hide`, while `Merge` and `WriteCopy` keep it visible, so the caller has to
 * observe an existing key: reporting a creation would let a caller which
 * initializes a key it believes to be new write defaults into the shadow key,
 * which wins against the real values of the host key.
 *
 * The rule is the create counterpart of `KeyHidesHost()`, which the open path
 * and the merged enumeration use, so the disposition cannot drift away from the
 * merged view.
 *
 * @param[in] mode The isolation mode of the key.
 * @param[in] hive_disposition The disposition reported by the create of the
 *                             state hive.
 * @param[in] image_holds_key Whether the image of the run holds the key.
 * @param[in] host_holds_key Whether the host layer holds the key. The value is
 *                           only consulted when the create made a new key and
 *                           the mode keeps the host entry visible.
 * @return `REG_CREATED_NEW_KEY` or `REG_OPENED_EXISTING_KEY` for the caller.
 */
inline ULONG ViewCreateDisposition(RegistryKeyIsolation mode, ULONG hive_disposition, bool image_holds_key,
                                   bool host_holds_key)
{
    if (hive_disposition != REG_CREATED_NEW_KEY)
    {
        /* The state hive holds the key, and it wins the merged view. */
        return hive_disposition;
    }

    if (image_holds_key)
    {
        /* The image is a sandbox layer: the key exists in the view of every
         * mode, whatever the mode says about the host registry. */
        return REG_OPENED_EXISTING_KEY;
    }

    if (KeyHidesHost(mode))
    {
        /* `Full` and `Hide`: the host entry is invisible, so the key is new for
         * the sandbox even when the host holds it. */
        return REG_CREATED_NEW_KEY;
    }

    if (host_holds_key)
    {
        /* `Merge` and `WriteCopy`: the host entry stays visible, so the key
         * already exists in the merged view. */
        return REG_OPENED_EXISTING_KEY;
    }

    return REG_CREATED_NEW_KEY;
}

/**
 * @brief What a delete of a key or of a value has to do in the merged view.
 *
 * The isolation modifies the real registry for `Merge` alone, so a delete which
 * the host layer would have received is either applied to the host entry (the
 * `Merge` rule) or turned into a **whiteout**: the entry of the state hive is
 * removed (when it holds one) and the entry of every layer below it is recorded
 * as deleted, so the merged view reports it as gone while the read-only layers
 * stay untouched.
 *
 * The decision table is shared by the delete of a key and the delete of a value;
 * the caller passes the effective mode of the entry, which is the listed value
 * mode for a value and the key mode for a key.
 *
 * | State hive holds | Image holds | Host holds | Mode | Result |
 * | --- | --- | --- | --- | --- |
 * | no | no | no | every mode | `ReportMissing` |
 * | no | no | yes | `Full`, `Hide` | `ReportMissing` |
 * | no | no | yes | `WriteCopy` | `WhiteoutOnly` |
 * | no | no | yes | `Merge` | `HostDelete` |
 * | no | yes | any | every mode | `SandboxAndWhiteout` |
 * | yes | no | no | every mode | `SandboxOnly` |
 * | yes | yes | any | every mode | `SandboxAndWhiteout` |
 * | yes | no | yes | every mode | `SandboxAndWhiteout` |
 *
 * An entry which no sandbox layer holds and which the host does not hold either
 * does not exist in the view, so the delete reports the failure of the state
 * hive, which is the answer the open of that entry gives. An entry of the image
 * is a sandbox entry: the state hive is modified (it holds a shadow key or it
 * does not, which makes the removal a no-op) and a marker hides the image, so
 * the entry stays invisible after the restart which rebuilds the image. An
 * entry the state hive holds is deleted from it for every mode, because the
 * state hive wins the merged view; the marker is recorded when a layer below
 * still holds the entry and would otherwise become visible again.
 *
 * `Merge` deletes the entry of the host registry when no sandbox layer holds
 * it, which is the write rule of the mode.
 *
 * A failed whiteout must fail the delete instead of reporting a success: the
 * entry of the layer below would otherwise reappear in the view.
 */
enum class DeleteTarget
{
    /**
     * @brief The entry does not exist in the view; the failure of the state
     *        hive layer is the result of the delete.
     */
    ReportMissing,

    /**
     * @brief Only the state hive holds a visible entry: the entry is deleted
     *        from it and nothing has to be recorded.
     */
    SandboxOnly,

    /**
     * @brief Only a layer below the state hive holds a visible entry: the entry
     *        is recorded as deleted (a whiteout), which is what the caller
     *        observes as a successful delete.
     */
    WhiteoutOnly,

    /**
     * @brief A layer below the state hive holds the entry: the state hive is
     *        modified and the entry of the layers below is recorded as deleted.
     */
    SandboxAndWhiteout,

    /**
     * @brief The entry of the real registry is deleted (`Merge`): no sandbox
     *        layer holds the entry, so the modification lands in the host.
     */
    HostDelete,
};

/**
 * @brief The delete route of a key or of a value in the merged view.
 *
 * The rows of the table are described by DeleteTarget. The rule agrees with
 * `KeyHidesHost()`: an entry whose host entry is invisible never reaches the
 * host registry, so the delete cannot succeed through it.
 *
 * @param[in] mode The isolation mode of the key.
 * @param[in] sandbox_holds Whether the state hive holds the entry.
 * @param[in] lower_holds Whether the image of the run holds the entry.
 * @param[in] host_holds Whether the real registry holds the entry and the mode
 *                       keeps it visible.
 * @return What the delete has to do.
 */
inline DeleteTarget KeyDeleteOutcomeOf(RegistryKeyIsolation mode, bool sandbox_holds, bool lower_holds, bool host_holds)
{
    if (!sandbox_holds && !lower_holds)
    {
        if (!host_holds)
        {
            return DeleteTarget::ReportMissing;
        }

        return mode == RegistryKeyIsolation::Merge ? DeleteTarget::HostDelete : DeleteTarget::WhiteoutOnly;
    }

    if (lower_holds || host_holds)
    {
        return DeleteTarget::SandboxAndWhiteout;
    }

    return DeleteTarget::SandboxOnly;
}

/**
 * @brief The delete route of a value in the merged view.
 *
 * The rows of the table are the ones of `KeyDeleteOutcomeOf()`; the effective
 * mode of the value decides about the host layer (`ValueHidesHost()`) and about
 * the layer the delete lands in (`ValueWritesToHost()`).
 *
 * @param[in] value_mode The listed mode of the value, empty when the file does
 *                       not list the value.
 * @param[in] key_mode The effective mode of the key which holds the value.
 * @param[in] sandbox_holds Whether the state hive holds the value.
 * @param[in] lower_holds Whether the image of the run holds the value.
 * @param[in] host_holds Whether the real registry holds the value and the mode
 *                       keeps it visible.
 * @return What the delete has to do.
 */
inline DeleteTarget ValueDeleteOutcomeOf(std::optional<RegistryValueIsolation> value_mode,
                                         RegistryKeyIsolation key_mode, bool sandbox_holds, bool lower_holds,
                                         bool host_holds)
{
    if (!sandbox_holds && !lower_holds)
    {
        if (!host_holds)
        {
            return DeleteTarget::ReportMissing;
        }

        return ValueWritesToHost(value_mode, key_mode, false) ? DeleteTarget::HostDelete : DeleteTarget::WhiteoutOnly;
    }

    if (lower_holds || host_holds)
    {
        return DeleteTarget::SandboxAndWhiteout;
    }

    return DeleteTarget::SandboxOnly;
}

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ISOLATIONPOLICY_HPP
