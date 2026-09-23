#ifndef APPBOX_SANDBOX_REGISTRY_INIT_HPP
#define APPBOX_SANDBOX_REGISTRY_INIT_HPP

#include "utils/WinAPI.h"
#include "RegistryIsolation.hpp"
#include "registry/IsolationTable.hpp"
#include <string>
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief Result of mapping the key of an OBJECT_ATTRIBUTES onto the view.
 */
enum class HiveMap
{
    /**
     * @brief The path is below none of the root keys of the view. The call
     *        must be forwarded unchanged.
     */
    NotIsolated,

    /**
     * @brief The path is below one of the root keys of the view. The logical
     *        view path and the path relative to the hive root were written to
     *        the output parameters.
     */
    Isolated,
};

/**
 * @brief Result of classifying a key handle against the isolation view.
 */
enum class HandleView
{
    /**
     * @brief The handle does not refer to the HKCU of the sandboxed user.
     */
    NotIsolated,

    /**
     * @brief The handle refers to a key inside the sandbox hive.
     */
    HiveHandle,

    /**
     * @brief The handle refers to a key of the real registry below one of the
     *        root keys of the view.
     */
    RealHandle,
};

/**
 * @brief Result of resolving a merged enumeration index onto a layer handle.
 */
enum class MergedResolve
{
    /**
     * @brief The merged view has no entry at the requested index.
     */
    NoMoreEntries,

    /**
     * @brief The entry lives in the hive layer; use the original handle.
     */
    HiveLayer,

    /**
     * @brief The entry lives in the real layer; use the returned handle.
     */
    RealLayer,

    /**
     * @brief The layer names could not be collected; forward the original call.
     */
    Error,
};

/**
 * @brief The sandbox registry hive.
 *
 * The hive is a real registry file mounted as a private application hive
 * (RegLoadAppKey) when the sandbox DLL is injected in isolation mode. It is
 * visible only inside the sandboxed process and can be opened exclusively
 * relative to the root handle, which is exactly what the redirection needs.
 *
 * The hive holds one sub key per root key of the view (`HKEY_LOCAL_MACHINE`,
 * `HKEY_CURRENT_USER`, ...), so the path of an entry inside the hive equals
 * its path in the registry workspace of the packer and in the isolation file.
 *
 * Redirection model (the decision table itself lives in
 * registry/IsolationPolicy.hpp, so it stays free of the registry API and unit
 * testable):
 * * Open (`OpenIsolatedKey` / `OpenIsolatedKeyEx`): every mode opens the key
 *   inside the hive first. When the hive does not hold the key the isolation
 *   mode decides (see `OpenFallback`): `Full` and `Hide` report the failure of
 *   the hive open, `WriteCopy` reads through the real registry and copies a
 *   write access open up into the hive.
 * * Create (`CreateIsolatedKey`, built on the raw `CreateKey`): every mode
 *   creates the key inside the hive, which also creates the intermediate
 *   keys. No mode ever modifies the real registry: every write of the
 *   sandboxed process lands in the hive. The disposition the caller observes
 *   follows the merged view of the mode (`ViewCreateDisposition`), so a key
 *   which only the host holds is reported as existing for `WriteCopy`, while
 *   `Full` and `Hide` report the creation of a key the host holds.
 * * The isolation modes of the isolation file decide which host entries stay
 *   invisible; the table defaults to `WriteCopy`, which is the read through of
 *   a sandbox without an isolation file.
 */
class Hive
{
public:
    struct Data;

    /**
     * @brief Mount the hive, collect the HKCU prefix and load the modes.
     *
     * The module is initialized before the hooks are attached, so the required
     * NT entry points are resolved locally instead of through the hook table.
     *
     * @return Status code. A failure is fatal for the sandbox, because the
     *         registry isolation would silently stop working.
     */
    static NTSTATUS Init();

    /**
     * @brief Release the hive mount.
     */
    static void Exit();

    /**
     * @brief Whether the hive was mounted successfully.
     * @return true when registry isolation is active.
     */
    static bool IsEnabled();

    /**
     * @brief Map the key described by ObjectAttributes onto the view.
     *
     * When the root handle refers to a key inside the sandbox hive, the path is
     * translated back into the logical view, so handles handed out by the
     * hooks behave exactly like the keys they shadow.
     *
     * @param[in] ObjectAttributes The object attributes of the hooked call.
     * @param[out] view_path The logical path of the key in the view, for
     *                       example `\REGISTRY\USER\<SID>\Software` or
     *                       `\REGISTRY\MACHINE\SOFTWARE`.
     * @param[out] relative The path of the key relative to the hive root, for
     *                      example `HKEY_CURRENT_USER\Software`.
     * @return The mapping result.
     */
    static HiveMap MapKeyPath(POBJECT_ATTRIBUTES ObjectAttributes, std::wstring& view_path,
                              std::wstring& relative);

    /**
     * @brief Convert a logical view path into the hive relative path.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[out] relative The hive relative path when the path is isolated.
     * @return true when the path belongs to one of the root keys of the view.
     */
    static bool HiveRelativePath(const std::wstring& view_path, std::wstring& relative);

    /**
     * @brief Get the effective isolation mode of a key.
     *
     * @param[in] relative The hive relative path of the key.
     * @return The mode of the key, `WriteCopy` when neither the key nor one of
     *         its ancestors is listed.
     */
    static RegistryIsolation KeyIsolation(const std::wstring& relative);

    /**
     * @brief Get the effective isolation mode of a value of a key.
     *
     * @param[in] relative The hive relative path of the key holding the value.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return The mode of the value.
     */
    static RegistryIsolation ValueIsolation(const std::wstring& relative, const std::wstring& value_name);

    /**
     * @brief Whether a value of a key may only be seen through the hive.
     *
     * @param[in] relative The hive relative path of the key holding the value.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return true when the host entry of the value must stay invisible.
     */
    static bool HidesHostValue(const std::wstring& relative, const std::wstring& value_name);

    /**
     * @brief Whether a key was deleted inside the sandbox (a whiteout).
     *
     * A delete never reaches the real registry, so it is recorded in the
     * whiteout store of the hive instead. The lookup walks the path upwards:
     * a deleted key hides its whole subtree, because the delete of a key
     * removes everything below it as well.
     *
     * @param[in] relative The hive relative path of the key.
     * @return true when the key or one of its ancestors was deleted.
     */
    static bool IsKeyWhitedOut(const std::wstring& relative);

    /**
     * @brief Whether a value was deleted inside the sandbox (a whiteout).
     *
     * @param[in] relative The hive relative path of the key holding the value.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return true when the value or the key which holds it was deleted.
     */
    static bool IsValueWhitedOut(const std::wstring& relative, const std::wstring& value_name);

    /**
     * @brief Drop the names of a real layer which hide the host registry.
     *
     * The merged enumeration view and the merged counts of a key have to agree
     * with the open behaviour: an entry whose isolation mode keeps the host
     * registry invisible must not be reported by either of them.
     *
     * @param[in] view_path The logical path of the parent key in the view.
     * @param[in] values true filters value names, false filters sub key names.
     * @param[in,out] names The names of the real layer, filtered in place.
     */
    static void FilterHiddenEntries(const std::wstring& view_path, bool values, std::vector<std::wstring>& names);

    /**
     * @brief Open an existing key inside the hive (NtOpenKey semantics).
     * @param[in] relative The key path relative to the hive root.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @return Status code.
     */
    static NTSTATUS OpenKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                            PVOID SecurityDescriptor, PVOID SecurityQualityOfService, PHANDLE KeyHandle);

    /**
     * @brief Open an existing key inside the hive (NtOpenKeyEx semantics).
     * @param[in] relative The key path relative to the hive root.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[in] OpenOptions The open options of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @return Status code.
     */
    static NTSTATUS OpenKeyEx(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                              PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                              PHANDLE KeyHandle);

    /**
     * @brief Open a key in the real registry through its view path (NtOpenKey semantics).
     *
     * This is the read through fallback for keys which the hive does not hold.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @return Status code.
     */
    static NTSTATUS OpenRealKey(const std::wstring& view_path, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                PVOID SecurityDescriptor, PVOID SecurityQualityOfService, PHANDLE KeyHandle);

    /**
     * @brief Open a key in the real registry through its view path (NtOpenKeyEx semantics).
     *
     * This is the read through fallback for keys which the hive does not hold.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[in] OpenOptions The open options of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @return Status code.
     */
    static NTSTATUS OpenRealKeyEx(const std::wstring& view_path, ACCESS_MASK DesiredAccess, ULONG Attributes,
                                  PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG OpenOptions,
                                  PHANDLE KeyHandle);

    /**
     * @brief Whether the real registry holds the key of a view path.
     *
     * Existence probe of the host layer: the key is opened through its view
     * path with the mask the merged view uses to reach the host layer
     * (`KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS`, the same pair the merged
     * counts of `NtQueryKey` and the merged enumeration use) and closed again.
     * A key which the host does not hold — and a key which denies that mask —
     * is reported as missing, which is the answer the merged view would give.
     *
     * The probe never changes the registry: the handle is closed before the
     * function returns.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @return true when the host layer holds a readable key of that name.
     */
    static bool HostHoldsKey(const std::wstring& view_path);

    /**
     * @brief Whether the real registry holds a value of a key.
     *
     * Existence probe of the host layer, the value counterpart of
     * `HostHoldsKey`: the real key is opened through its view path with
     * `KEY_QUERY_VALUE` and the value is queried with the basic information
     * class, so a value which is there but too large for the probe buffer is
     * still reported as existing. The probe never changes the registry.
     *
     * The probe does not consult the isolation mode nor the whiteout store: it
     * answers for the raw host layer, and the callers apply the visibility
     * rules of the merged view.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return true when the host layer holds the value.
     */
    static bool HostHoldsValue(const std::wstring& view_path, const std::wstring& value_name);

    /**
     * @brief Open an isolated key with the route of its isolation mode (NtOpenKey semantics).
     *
     * The call runs the whole open policy of the registry isolation, so the
     * hooks only have to resolve the path and delegate:
     *
     * * The hive is opened first; when it does not hold the key, `Full` and
     *   `Hide` report that failure, `WriteCopy` reads the key through the real
     *   registry or copies a write access open up into the hive (see
     *   `OpenFallback`).
     *
     * A key which neither layer holds is never created: an open reports the
     * failure of the hive, even for a write access mask.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] relative The key path relative to the hive root.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @return Status code.
     */
    static NTSTATUS OpenIsolatedKey(const std::wstring& view_path, const std::wstring& relative,
                                    ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                    PVOID SecurityQualityOfService, PHANDLE KeyHandle);

    /**
     * @brief Open an isolated key with the route of its isolation mode (NtOpenKeyEx semantics).
     *
     * Same policy as OpenIsolatedKey(), with the open options of the caller
     * forwarded to every attempt.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] relative The key path relative to the hive root.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[in] OpenOptions The open options of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @return Status code.
     */
    static NTSTATUS OpenIsolatedKeyEx(const std::wstring& view_path, const std::wstring& relative,
                                      ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                      PVOID SecurityQualityOfService, ULONG OpenOptions, PHANDLE KeyHandle);

    /**
     * @brief Create or open a key inside the hive (NtCreateKey semantics).
     *
     * The path is walked component by component and every intermediate key is
     * created inside the hive, so a create also works for a path which only
     * the real registry holds so far. `Disposition` reports the last
     * component, which is the one the caller asked for. Every isolation mode
     * takes this route: a create never touches the real registry.
     *
     * @param[in] relative The key path relative to the hive root.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[in] TitleIndex The title index of the original call.
     * @param[in] Class The key class of the original call.
     * @param[in] CreateOptions The create options of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @param[out] Disposition REG_CREATED_NEW_KEY or REG_OPENED_EXISTING_KEY, may be null.
     * @return Status code.
     */
    static NTSTATUS CreateKey(const std::wstring& relative, ACCESS_MASK DesiredAccess, ULONG Attributes,
                              PVOID SecurityDescriptor, PVOID SecurityQualityOfService, ULONG TitleIndex,
                              PUNICODE_STRING Class, ULONG CreateOptions, PHANDLE KeyHandle,
                              PULONG Disposition);

    /**
     * @brief Create or open an isolated key with the route of its isolation mode (NtCreateKey semantics).
     *
     * The raw create (`CreateKey`) builds the key inside the hive for every
     * mode, which is what keeps the real registry untouched. This entry point
     * runs the create policy of the isolation on top of it: the disposition
     * reports the merged view of the caller instead of the hive layer alone
     * (see `appbox::registry::ViewCreateDisposition`), so a key which only the
     * host holds is reported as existing for a mode which keeps the host entry
     * visible. `Full` and `Hide` keep the disposition of the hive, because the
     * host entry is invisible for them.
     *
     * The host layer is only probed when the hive created the key and the mode
     * keeps the host entry visible, so a create of a key which the hive
     * already holds costs no extra call. A failed probe is not a failed
     * create: the handle and the status are the ones of the hive create.
     *
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] relative The key path relative to the hive root.
     * @param[in] DesiredAccess The requested access mask.
     * @param[in] Attributes The object attributes flags of the original call.
     * @param[in] SecurityDescriptor The security descriptor of the original call.
     * @param[in] SecurityQualityOfService The quality of service of the original call.
     * @param[in] TitleIndex The title index of the original call.
     * @param[in] Class The key class of the original call.
     * @param[in] CreateOptions The create options of the original call.
     * @param[out] KeyHandle The resulting key handle.
     * @param[out] Disposition `REG_CREATED_NEW_KEY` or
     *                         `REG_OPENED_EXISTING_KEY` as seen by the caller,
     *                         may be null.
     * @return Status code.
     */
    static NTSTATUS CreateIsolatedKey(const std::wstring& view_path, const std::wstring& relative,
                                      ACCESS_MASK DesiredAccess, ULONG Attributes, PVOID SecurityDescriptor,
                                      PVOID SecurityQualityOfService, ULONG TitleIndex, PUNICODE_STRING Class,
                                      ULONG CreateOptions, PHANDLE KeyHandle, PULONG Disposition);

    /**
     * @brief Delete a key of the merged view (NtDeleteKey semantics).
     *
     * The isolation never modifies the real registry, so the delete runs
     * against the view: the key of the hive is removed and the key of the host
     * is recorded as deleted (a whiteout), which keeps it invisible for every
     * later open, enumeration and query. A key which only the host holds is
     * therefore deleted as well — without the whiteout the read through would
     * resurrect it.
     *
     * The route of the delete is the decision table
     * `appbox::registry::DeleteOutcomeOf`. A key which holds a visible sub key
     * is refused with `STATUS_CANNOT_DELETE`, which is what the real
     * `NtDeleteKey` reports (Win32 maps it to `ERROR_ACCESS_DENIED`); values do
     * not block a delete. A failed whiteout fails the whole call, because
     * reporting a success would let the host entry reappear in the view.
     *
     * @param[in] KeyHandle The hive key handle of the caller. The kernel checks
     *                      its rights, so a handle which does not permit a
     *                      delete reports the failure of the real call.
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] relative The key path relative to the hive root.
     * @return Status code.
     */
    static NTSTATUS DeleteIsolatedKey(HANDLE KeyHandle, const std::wstring& view_path, const std::wstring& relative);

    /**
     * @brief Delete a value of the merged view (NtDeleteValueKey semantics).
     *
     * Same rule as the delete of a key: the value of the hive is removed and a
     * value which only the host holds is recorded as deleted, so the read
     * through of the value and the merged value enumeration no longer report
     * it while the real registry keeps its value.
     *
     * @param[in] KeyHandle The hive key handle of the caller. The kernel checks
     *                      its rights, so a handle which does not permit the
     *                      delete reports the failure of the real call.
     * @param[in] view_path The logical path of the key which holds the value.
     * @param[in] relative The key path relative to the hive root.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return Status code.
     */
    static NTSTATUS DeleteIsolatedValue(HANDLE KeyHandle, const std::wstring& view_path, const std::wstring& relative,
                                        const std::wstring& value_name);

    /**
     * @brief Query several values of the merged view (NtQueryMultipleValueKey semantics).
     *
     * Every entry is answered by the layer which holds it: the hive layer wins,
     * a value which only the host holds is read through — unless the isolation
     * mode or a whiteout keeps the host entry invisible, in which case the
     * whole call reports `STATUS_OBJECT_NAME_NOT_FOUND`, which is what the
     * kernel reports for a missing value of a batch.
     *
     * A batch which comes from a single layer is forwarded to that layer with
     * the caller parameters, so the kernel computes the data offsets. A batch
     * which mixes both layers is assembled here: every value is read from its
     * layer and copied into the caller buffer in the order of the entries, with
     * the data offsets the kernel would have produced (the entries are packed
     * without padding).
     *
     * @param[in] KeyHandle The hive key handle of the hooked call.
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] relative The key path relative to the hive root.
     * @param[in,out] ValueEntries The entries of the call, filled with the data
     *                             length, the data offset and the type.
     * @param[in] EntryCount The number of entries.
     * @param[out] ValueBuffer The caller buffer which receives the data.
     * @param[in,out] BufferLength The size of the caller buffer, set to the
     *                             used size.
     * @param[out] RequiredBufferLength The size the buffer needs.
     * @return Status code.
     */
    static NTSTATUS QueryMultipleValues(HANDLE KeyHandle, const std::wstring& view_path, const std::wstring& relative,
                                        PKEY_VALUE_ENTRY ValueEntries, ULONG EntryCount, PVOID ValueBuffer,
                                        PULONG BufferLength, PULONG RequiredBufferLength);

    /**
     * @brief Save a key of the merged view into a hive file (NtSaveKey semantics).
     *
     * The saved file holds the merged view of the key at the time of the call:
     * the entries of the hive layer plus the host entries which the isolation
     * mode keeps visible. Entries which a mode or a whiteout hides are not part
     * of the file, so a save never exports data the sandboxed process cannot
     * see.
     *
     * The snapshot is built in a temporary hive next to the sandbox hive: the
     * merged content is copied into it and that key is saved, which produces a
     * regular hive file the caller can restore. The temporary hive and its
     * transaction logs are removed before the call returns; the mount of a hive
     * file resolves its path outside the filesystem view, so the file of the
     * overlay is removed through the raw delete entry point.
     *
     * A key whose real layer contributes nothing to the view is saved directly
     * from its hive key, which is the common case and costs no extra file.
     *
     * @param[in] KeyHandle The hive key handle of the caller, null when the
     *                      caller holds a real read through handle.
     * @param[in] view_path The logical path of the key in the view.
     * @param[in] relative The key path relative to the hive root.
     * @param[in] FileHandle The file which receives the hive, opened by the caller.
     * @param[in] Format The save format of the caller (NtSaveKeyEx).
     * @param[in] extended true runs NtSaveKeyEx, false NtSaveKey.
     * @return Status code.
     */
    static NTSTATUS SaveIsolatedKey(HANDLE KeyHandle, const std::wstring& view_path, const std::wstring& relative,
                                    HANDLE FileHandle, ULONG Format, bool extended);

    /**
     * @brief Classify a key handle against the isolation view.
     *
     * The object name of the handle is queried and mapped back into the
     * logical view: handles below the private hive mount are HiveHandle with
     * their view path reconstructed, handles below the real HKCU root are
     * RealHandle, everything else is NotIsolated.
     *
     * @param[in] KeyHandle The key handle of the hooked call.
     * @param[out] view_path The logical view path of the key,
     *                       \REGISTRY\USER\<SID>\....
     * @return The classification of the handle.
     */
    static HandleView MapHandleView(HANDLE KeyHandle, std::wstring& view_path);

    /**
     * @brief Translate an object name of the private hive mount into the view.
     *
     * @param[in] object_name The object name, for example the key path
     *                        reported by NtQueryKey or NtQueryObject.
     * @param[out] view_name The name with the hive mount prefix replaced by
     *                       the HKCU prefix.
     * @return true when the name is below the hive mount and was translated.
     */
    static bool TranslateHiveObjectName(const std::wstring& object_name, std::wstring& view_name);

    /**
     * @brief Collect the sub key names of a key handle.
     *
     * Runs a complete KeyBasicInformation enumeration of the given handle. The
     * function is only used on the hook path, so the system entry points of
     * the hook table are available.
     *
     * The collection is complete and in enumeration order, so the position of
     * an entry is the index the kernel assigned to it. The merged enumeration
     * relies on that (see ResolveMergedIndex), so an entry must never be
     * skipped.
     *
     * @param[in] KeyHandle The key handle, hive or real.
     * @param[out] names The sub key names in enumeration order.
     * @return Status code. STATUS_NO_MORE_ENTRIES is consumed internally.
     */
    static NTSTATUS CollectSubKeyNames(HANDLE KeyHandle, std::vector<std::wstring>& names);

    /**
     * @brief Collect the value names of a key handle.
     *
     * Runs a complete KeyValueBasicInformation enumeration of the given
     * handle. The function is only used on the hook path, so the system entry
     * points of the hook table are available.
     *
     * The collection is complete and in enumeration order, so the position of
     * an entry is the index the kernel assigned to it. The default value of
     * the key is enumerated with an empty name and is part of the collection
     * like every other value; the merged enumeration relies on the complete
     * order (see ResolveMergedIndex).
     *
     * @param[in] KeyHandle The key handle, hive or real.
     * @param[out] names The value names in enumeration order, the empty name
     *                   for the default value.
     * @return Status code. STATUS_NO_MORE_ENTRIES is consumed internally.
     */
    static NTSTATUS CollectValueNames(HANDLE KeyHandle, std::vector<std::wstring>& names);

    /**
     * @brief Resolve a merged enumeration index of a hive key handle.
     *
     * Collects the entry names of the hive layer and of the real key which the
     * view path addresses, drops the real entries whose isolation mode hides
     * the host registry, then maps the merged index onto the holding layer
     * (see registry/EnumMerge.hpp for the merge rules).
     *
     * @param[in] KeyHandle The hive key handle of the hooked call.
     * @param[in] view_path The logical view path of the key.
     * @param[in] values true resolves value names (NtEnumerateValueKey), false
     *                   resolves sub key names (NtEnumerateKey).
     * @param[in] Index The index inside the merged view.
     * @param[out] real_handle The handle of the real key, owned by the caller.
     *                         Null unless the result is RealLayer.
     * @param[out] layer_index The index of the entry inside its layer.
     * @return The resolution result. Every result except RealLayer leaves
     *         real_handle null.
     */
    static MergedResolve ResolveMergedIndex(HANDLE KeyHandle, const std::wstring& view_path, bool values, ULONG Index,
                                            HANDLE& real_handle, ULONG& layer_index);
};

/**
 * @brief Read the value name of a hooked value call.
 *
 * The buffer of an UNICODE_STRING is not required to be null terminated and a
 * caller can pass a length which does not fit into the buffer it owns. Such a
 * structure is rejected instead of being read, because a hook must never read
 * outside the memory of the caller.
 *
 * @param[in] ValueName The value name of the call, may be null.
 * @param[out] name The name of the value, empty for the default value.
 * @return true when the name is usable, false when the call has to be
 *         forwarded without an isolation decision.
 */
bool ReadValueName(PUNICODE_STRING ValueName, std::wstring& name);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_INIT_HPP
