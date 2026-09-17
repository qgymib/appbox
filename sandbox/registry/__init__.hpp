#ifndef APPBOX_SANDBOX_REGISTRY_INIT_HPP
#define APPBOX_SANDBOX_REGISTRY_INIT_HPP

#include "utils/WinAPI.h"
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
     * @brief The path is not inside HKCU. The call must be forwarded unchanged.
     */
    NotHkcu,

    /**
     * @brief The path is inside HKCU. The logical view path and the path
     *        relative to the hive root were written to the output parameters.
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
     * @brief The handle refers to a key of the real HKCU.
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
 * HKCU redirection model:
 * * Open (read entry point): open the key inside the hive first; when it is
 *   not there, fall back to the real registry (read through).
 * * Create (write entry point): always create the key inside the hive, which
 *   also creates the intermediate keys, and never touch the real registry.
 */
class Hive
{
public:
    struct Data;

    /**
     * @brief Mount the hive and collect the HKCU prefix of the current user.
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
     * @param[out] view_path The logical path of the key in the view,
     *                       \REGISTRY\USER\<SID>\....
     * @param[out] relative The path of the key relative to the hive root.
     * @return The mapping result.
     */
    static HiveMap MapKeyPath(POBJECT_ATTRIBUTES ObjectAttributes, std::wstring& view_path,
                              std::wstring& relative);

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
     * @brief Create or open a key inside the hive (NtCreateKey semantics).
     *
     * NtCreateKey creates the intermediate keys of a multi component relative
     * path automatically, so no separate path walk is needed.
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
     * @param[in] KeyHandle The key handle, hive or real.
     * @param[out] names The value names in enumeration order.
     * @return Status code. STATUS_NO_MORE_ENTRIES is consumed internally.
     */
    static NTSTATUS CollectValueNames(HANDLE KeyHandle, std::vector<std::wstring>& names);

    /**
     * @brief Resolve a merged enumeration index of a hive key handle.
     *
     * Collects the entry names of the hive layer and of the real key which the
     * view path addresses, then maps the merged index onto the holding layer
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

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_INIT_HPP
