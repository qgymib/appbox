#ifndef APPBOX_SANDBOX_REGISTRY_ISOLATIONPOLICY_HPP
#define APPBOX_SANDBOX_REGISTRY_ISOLATIONPOLICY_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "RegistryIsolation.hpp"
#include "registry/IsolationTable.hpp"

namespace appbox
{
namespace registry
{

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
 * | `WriteCopy` | `UseHost` | `CopyUp` |
 * | `Hide` | `ReportHiveFailure` | `ReportHiveFailure` |
 *
 * The rows of `Full` and `Hide` are the same rule `IsolationTable::HidesHost()`
 * applies to the host entries: the host entry of such a key must not be used.
 */
enum class OpenFallback
{
    /**
     * @brief The host entry must not be used; the failure of the hive open is
     *        the result of the call.
     */
    ReportHiveFailure,

    /**
     * @brief The key is read through the host registry.
     */
    UseHost,

    /**
     * @brief The key is copied up: a shadow key is created inside the hive and
     *        handed out, so every modification lands in the sandbox.
     */
    CopyUp,
};

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
 * An unnecessary copy-up costs one empty shadow key inside the hive, which the
 * merged enumeration and the value read through hide completely, while a missed
 * write access would let a modification escape into the host registry.
 *
 * @param[in] access The access mask of the open.
 * @return true when the mask may permit a modification of the key.
 */
inline bool RequestsWrite(ACCESS_MASK access)
{
    constexpr ACCESS_MASK kWrite =
        KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK | DELETE | WRITE_DAC | WRITE_OWNER | MAXIMUM_ALLOWED
        | GENERIC_WRITE | GENERIC_ALL;
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
 * @brief The fallback of an isolated open whose hive layer does not hold the key.
 *
 * The rows of the table are described by OpenFallback; the rule of `Full` and
 * `Hide` agrees with `IsolationTable::HidesHost()`, so a key whose host entry
 * is invisible never reaches the host registry.
 *
 * @param[in] mode The isolation mode of the key.
 * @param[in] desired_access The access mask of the open.
 * @return The fallback the open has to run.
 */
inline OpenFallback FallbackForKey(RegistryIsolation mode, ACCESS_MASK desired_access)
{
    switch (mode)
    {
    case RegistryIsolation::Full:
    case RegistryIsolation::Hide:
        /* The host entry is invisible for the sandbox. */
        return OpenFallback::ReportHiveFailure;
    case RegistryIsolation::WriteCopy:
        /* The sandbox takes precedence: every modification lands in the hive. */
        return RequestsWrite(desired_access) ? OpenFallback::CopyUp : OpenFallback::UseHost;
    }
    return OpenFallback::ReportHiveFailure;
}

/**
 * @brief The disposition a create reports for the merged view.
 *
 * A create always lands in the sandbox hive, so the disposition of the kernel
 * describes the hive layer alone. The view of the sandboxed process is the
 * hive layer merged with the visible entries of the host layer, so the value
 * the caller observes has to be derived from both layers:
 *
 * | Hive disposition | Mode | Host holds the key | Reported disposition |
 * | --- | --- | --- | --- |
 * | `REG_OPENED_EXISTING_KEY` | every mode | not consulted | `REG_OPENED_EXISTING_KEY` |
 * | `REG_CREATED_NEW_KEY` | `Full`, `Hide` | not consulted | `REG_CREATED_NEW_KEY` |
 * | `REG_CREATED_NEW_KEY` | `WriteCopy` | yes | `REG_OPENED_EXISTING_KEY` |
 * | `REG_CREATED_NEW_KEY` | `WriteCopy` | no | `REG_CREATED_NEW_KEY` |
 *
 * A key which the hive already holds exists in the view of every mode, so the
 * hive disposition is reported unchanged: the hive wins the merged view. A key
 * which the hive just created does not exist in the view of `Full` and `Hide`,
 * because those modes keep the host entry invisible — reporting a creation is
 * exactly what the sandboxed process has to observe. `WriteCopy` keeps the host
 * entry visible, so a key which only the host holds is an existing key for the
 * caller: reporting a creation would let a caller which initializes a key it
 * believes to be new write defaults into the shadow key, which the hive wins
 * against the real values of the host key.
 *
 * The rule is the create counterpart of `IsolationTable::HidesHost()`, which
 * the open path and the merged enumeration use, so the disposition cannot
 * drift away from the merged view.
 *
 * @param[in] mode The isolation mode of the key.
 * @param[in] hive_disposition The disposition reported by the hive create.
 * @param[in] host_holds_key Whether the host layer holds the key. The value is
 *                           only consulted when the hive created the key and
 *                           the mode keeps the host entry visible.
 * @return `REG_CREATED_NEW_KEY` or `REG_OPENED_EXISTING_KEY` for the caller.
 */
inline ULONG ViewCreateDisposition(RegistryIsolation mode, ULONG hive_disposition, bool host_holds_key)
{
    if (hive_disposition != REG_CREATED_NEW_KEY)
    {
        /* The hive holds the key, and the hive wins the merged view. */
        return hive_disposition;
    }

    if (IsolationTable::HidesHost(mode))
    {
        /* `Full` and `Hide`: the host entry is invisible, so the key is new for
         * the sandbox even when the host holds it. */
        return REG_CREATED_NEW_KEY;
    }

    if (host_holds_key)
    {
        /* `WriteCopy`: the host entry stays visible, so the key already exists
         * in the merged view. */
        return REG_OPENED_EXISTING_KEY;
    }

    return REG_CREATED_NEW_KEY;
}

/**
 * @brief What a delete of a key or of a value has to do in the merged view.
 *
 * The isolation never modifies the real registry, so a delete which the host
 * layer would have received has to be turned into a **whiteout** instead: the
 * entry of the hive is removed (when the hive holds one) and the entry of the
 * host is recorded as deleted, so the merged view reports it as gone while the
 * real registry stays untouched.
 *
 * The decision table is shared by the delete of a key and the delete of a
 * value; the caller passes the effective mode of the entry, which is
 * `ValueIsolation()` for a value and `KeyIsolation()` for a key.
 *
 * | Hive holds the entry | Host holds the entry | Mode | Result |
 * | --- | --- | --- | --- |
 * | no | no | every mode | `ReportMissing` |
 * | no | yes | `Full`, `Hide` | `ReportMissing` |
 * | no | yes | `WriteCopy` | `WhiteoutOnly` |
 * | yes | no | every mode | `HiveOnly` |
 * | yes | yes | `Full`, `Hide` | `HiveOnly` |
 * | yes | yes | `WriteCopy` | `HiveAndWhiteout` |
 *
 * An entry which only the host holds does not exist for a mode which keeps the
 * host entry invisible, so the delete reports the failure of the hive layer —
 * the same answer the open of that entry gives. An entry which the hive holds
 * is deleted from the hive for every mode, because the hive wins the merged
 * view. The whiteout is only recorded when the host entry would otherwise
 * become visible again, which is exactly `WriteCopy`.
 *
 * A failed whiteout must fail the delete instead of reporting a success: the
 * host entry would otherwise reappear in the view.
 */
enum class DeleteTarget
{
    /**
     * @brief The entry does not exist in the view; the failure of the hive
     *        layer is the result of the delete.
     */
    ReportMissing,

    /**
     * @brief Only the hive layer holds a visible entry: the entry is deleted
     *        from the hive and nothing has to be recorded.
     */
    HiveOnly,

    /**
     * @brief Only the host layer holds a visible entry: the entry is recorded
     *        as deleted (a whiteout), which is what the caller observes as a
     *        successful delete.
     */
    WhiteoutOnly,

    /**
     * @brief Both layers hold a visible entry: the entry of the hive is
     *        deleted and the entry of the host is recorded as deleted.
     */
    HiveAndWhiteout,
};

/**
 * @brief The delete route of an entry in the merged view.
 *
 * The rows of the table are described by DeleteTarget. The rule agrees with
 * `IsolationTable::HidesHost()`: an entry whose host entry is invisible never
 * reaches the host registry, so the delete cannot succeed through it.
 *
 * @param[in] mode The effective isolation mode of the entry.
 * @param[in] hive_holds Whether the sandbox hive holds the entry.
 * @param[in] host_holds Whether the real registry holds the entry.
 * @return What the delete has to do.
 */
inline DeleteTarget DeleteOutcomeOf(RegistryIsolation mode, bool hive_holds, bool host_holds)
{
    if (!host_holds)
    {
        /* Without a host entry there is nothing which could reappear. */
        return hive_holds ? DeleteTarget::HiveOnly : DeleteTarget::ReportMissing;
    }

    if (IsolationTable::HidesHost(mode))
    {
        /* The host entry is invisible for the sandbox, so the entry does not
         * exist in its view at all. */
        return hive_holds ? DeleteTarget::HiveOnly : DeleteTarget::ReportMissing;
    }

    return hive_holds ? DeleteTarget::HiveAndWhiteout : DeleteTarget::WhiteoutOnly;
}

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ISOLATIONPOLICY_HPP
