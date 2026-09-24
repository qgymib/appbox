#ifndef APPBOX_SANDBOX_FILESYSTEM_DIRECTORYMERGE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_DIRECTORYMERGE_HPP

#include "utils/WinAPI.h"

namespace appbox
{
namespace filesystem
{

/**
 * @brief Flag which restarts the scan of a directory (`SL_RESTART_SCAN`).
 */
inline constexpr ULONG kQueryRestartScan = 0x00000001;

/**
 * @brief Flag which makes a query report one entry (`SL_RETURN_SINGLE_ENTRY`).
 */
inline constexpr ULONG kQueryReturnSingleEntry = 0x00000002;

/**
 * @brief Whether the merge understands an information class.
 *
 * The view of a directory is the merge of every layer which holds it, which
 * only an entry list the merge can read and rewrite allows: the classes which
 * carry the name of an entry are supported, every other class is forwarded to
 * the layer the handle was opened with.
 *
 * @param[in] info_class Information class of a directory query.
 * @return true when the class can be merged and filtered.
 */
bool IsSupportedDirectoryInformationClass(FILE_INFORMATION_CLASS info_class);

/**
 * @brief Query a directory of the view with the merged content of its layers.
 *
 * The function is shared by `NtQueryDirectoryFileEx` and
 * `NtQueryDirectoryFile`: the layers which hold the directory (see
 * `ResolveResult::hPath`) are enumerated one after the other and the names
 * which were already reported are dropped. The entries a whiteout, an opaque
 * marker or the isolation of the view hides are filtered out, which keeps the
 * enumeration and the isolation of a single path consistent.
 *
 * The state of an enumeration is kept per handle, so a caller may mix the two
 * entry points and the information classes on the same handle;
 * `kQueryRestartScan` discards the state and starts at the upper layer again.
 *
 * The handle has to be registered by `NtOpenFile` and the class has to be
 * supported; the caller checks both before it calls.
 *
 * @param[in] FileHandle Handle of the directory, registered by `NtOpenFile`.
 * @param[in] IoStatusBlock Status block of the call.
 * @param[in] FileInformation Caller buffer which receives the entries.
 * @param[in] Length Size of the caller buffer in bytes.
 * @param[in] QueryFlags `kQueryRestartScan` and `kQueryReturnSingleEntry`.
 * @param[in] FileName Search pattern of the call, may be null.
 * @param[in] FileInformationClass Information class of the call.
 * @param[in] extended true when the caller used the extended entry point.
 * @return Status code.
 */
NTSTATUS QueryDirectoryInformation(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
                                   ULONG QueryFlags, PUNICODE_STRING FileName,
                                   FILE_INFORMATION_CLASS FileInformationClass, bool extended);

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_DIRECTORYMERGE_HPP
