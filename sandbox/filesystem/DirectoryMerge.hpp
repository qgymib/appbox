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
 * @brief Query a directory of the view with the merged content of its layers.
 *
 * The function is shared by `NtQueryDirectoryFileEx` and
 * `NtQueryDirectoryFile`: the layers which hold the directory (see
 * `ResolveResult::hPath`) are enumerated one after the other and the names
 * which were already reported are dropped. The entries a whiteout, an opaque
 * marker or the isolation of the view hides are filtered out, which keeps the
 * enumeration and the isolation of a single path consistent.
 *
 * The layers come from the record the sandbox wrote while it opened the handle,
 * and a handle the sandbox did not open, which is the handle a process
 * inherited or duplicated, is adopted: the path of the view it denotes is
 * looked up in the file system (see `ViewPathOfHandle`), which is what makes an
 * enumeration through such a handle report the same view as an enumeration
 * through a handle the sandbox opened. A handle which denotes no directory of
 * the view is reported through \p handled, so the caller forwards its call
 * unchanged.
 *
 * A class which carries no name of an entry cannot be merged (see
 * `DirectoryInformationLayoutOf`), and the view answers it with
 * `STATUS_NOT_SUPPORTED` instead of forwarding the call: the answer of the
 * layer the handle was opened with would show the entries the view hides and
 * the markers of the view themselves, so the view never answers with the
 * content of a single layer. The same reason makes the view refuse a directory
 * the view does not hold with `STATUS_OBJECT_NAME_NOT_FOUND` and a local object
 * the view cannot name with `STATUS_NOT_SUPPORTED`.
 *
 * The state of an enumeration is kept per handle, so a caller may mix the two
 * entry points and the information classes on the same handle;
 * `kQueryRestartScan` discards the state and starts at the upper layer again.
 *
 * @param[in] FileHandle Handle of the directory.
 * @param[in] IoStatusBlock Status block of the call.
 * @param[in] FileInformation Caller buffer which receives the entries.
 * @param[in] Length Size of the caller buffer in bytes.
 * @param[in] QueryFlags `kQueryRestartScan` and `kQueryReturnSingleEntry`.
 * @param[in] FileName Search pattern of the call, may be null.
 * @param[in] FileInformationClass Information class of the call.
 * @param[in] extended true when the caller used the extended entry point.
 * @param[out] handled true when the view answered the call, false when the
 *             handle denotes no directory of the view and the caller has to
 *             forward its call unchanged.
 * @return Status code of the answer, which carries no meaning when \p handled
 *         is false.
 */
NTSTATUS QueryDirectoryInformation(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                   ULONG Length, ULONG QueryFlags, PUNICODE_STRING FileName,
                                   FILE_INFORMATION_CLASS FileInformationClass, bool extended, bool& handled);

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_DIRECTORYMERGE_HPP
