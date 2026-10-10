#include "utils/WinAPI.h" /* Must be first include file */
#include "filesystem/ReparsePoint.hpp"
#include "utils/Log.hpp"
#include "NtFsControlFile.hpp"
#include <vector>

T_NtFsControlFile sys_NtFsControlFile = nullptr;

static nlohmann::json NtFsControlFileLogParam(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                              PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode,
                                              PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer,
                                              ULONG OutputBufferLength)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Event"] = appbox::PointerToString(Event);
    param["ApcRoutine"] = appbox::PointerToString(ApcRoutine);
    param["ApcContext"] = appbox::PointerToString(ApcContext);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FsControlCode"] = FsControlCode;
    param["InputBuffer"] = appbox::PointerToString(InputBuffer);
    param["InputBufferLength"] = InputBufferLength;
    param["OutputBuffer"] = appbox::PointerToString(OutputBuffer);
    param["OutputBufferLength"] = OutputBufferLength;
    return param;
}

static appbox::LoggerF logger("NtFsControlFile", NtFsControlFileLogParam);

/**
 * @brief Translate the data of a reparse point which a caller writes.
 *
 * The data names the target of the link in the namespace the caller knows,
 * which may be the namespace of a layer: the file system reports the path of
 * the layer for a handle, so an application which links an object it holds
 * would otherwise store the layout of the sandbox in the view. The translated
 * data replaces the buffer of the caller, so the view reads back the path it
 * stored when it follows the link.
 *
 * @param[in] InputBuffer Buffer of the caller.
 * @param[in] InputBufferLength Size of the buffer of the caller.
 * @param[out] translated Data of the view, empty when the call is forwarded
 *                        unchanged.
 * @param[out] status Status the call reports when the data cannot be used.
 * @return true when the call is forwarded, false when the caller reports
 *         \p status instead.
 */
static bool TranslateSetReparsePoint(PVOID InputBuffer, ULONG InputBufferLength, std::vector<BYTE>& translated,
                                     NTSTATUS& status)
{
    if (InputBuffer == nullptr || InputBufferLength < sizeof(ULONG) ||
        InputBufferLength > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        /* The file system reports the failure of a buffer it cannot use. */
        return true;
    }

    const auto*             begin = static_cast<const BYTE*>(InputBuffer);
    const std::vector<BYTE> data(begin, begin + InputBufferLength);

    status = appbox::filesystem::TranslateReparseDataToView(data, translated);
    if (!NT_SUCCESS(status))
    {
        return false;
    }

    if (translated == data)
    {
        /* The data already names a path of the view, the caller keeps its
         * buffer. */
        translated.clear();
    }

    return true;
}

static NTSTATUS Hook_NtFsControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                     PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode, PVOID InputBuffer,
                                     ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength)
{
    logger.Log(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FsControlCode, InputBuffer, InputBufferLength,
               OutputBuffer, OutputBufferLength);

    /*
     * The handle denotes the layer the view selected, so the object of the
     * call is already the right one and only the data of a reparse point has
     * to be translated: the target a caller writes is stored in the namespace
     * of the view, which is what the view reads back when it follows the link.
     * A control code which carries no reparse point reaches the object of the
     * layer unchanged.
     */
    if (FsControlCode == FSCTL_SET_REPARSE_POINT)
    {
        std::vector<BYTE> translated;
        NTSTATUS          status = STATUS_SUCCESS;
        if (!TranslateSetReparsePoint(InputBuffer, InputBufferLength, translated, status))
        {
            return status;
        }

        if (!translated.empty())
        {
            return sys_NtFsControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FsControlCode,
                                       translated.data(), static_cast<ULONG>(translated.size()), OutputBuffer,
                                       OutputBufferLength);
        }
    }

    return sys_NtFsControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FsControlCode, InputBuffer,
                               InputBufferLength, OutputBuffer, OutputBufferLength);
}

static void LoadNtFsControlFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtFsControlFile");
    sys_NtFsControlFile = reinterpret_cast<T_NtFsControlFile>(addr);
}

appbox::HookRecord appbox::HookNtFsControlFile = {
    "NtFsControlFile",
    LoadNtFsControlFile,
    (void**)&sys_NtFsControlFile,
    Hook_NtFsControlFile,
};
