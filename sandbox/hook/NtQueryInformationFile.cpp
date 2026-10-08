#include "utils/WinAPI.h" /* Must be first include file */
#include "filesystem/FileInformationClass.hpp"
#include "filesystem/LayerPath.hpp"
#include "utils/Log.hpp"
#include "NtQueryInformationFile.hpp"
#include <cstddef>
#include <vector>

T_NtQueryInformationFile sys_NtQueryInformationFile = nullptr;

/**
 * @brief The layout of `FileAllInformation` is the one the file system reports.
 *
 * The name is the last member of the record, so the hook rewrites the tail of
 * the answer in place. The offset is pinned here because the name of the
 * record is the only part of the layout the hook depends on.
 */
static_assert(offsetof(FILE_ALL_INFORMATION, NameInformation) == 96,
              "the name of FileAllInformation must be the last member of the record");

/**
 * @brief Size of the local buffer which holds the answer of a name class.
 *
 * The buffer is only used when the answer does not fit the buffer of the
 * caller: the name of a layer is longer than the name of the view it belongs
 * to, so a caller whose buffer is too small for the layer may still have room
 * for the view.
 */
static const ULONG kNameBufferSize = 0x1000;

static nlohmann::json NtQueryInformationFileLogParam(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                     PVOID FileInformation, ULONG Length,
                                                     FILE_INFORMATION_CLASS FileInformationClass)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = appbox::filesystem::FileInformationClassName(FileInformationClass);
    return param;
}
static appbox::LoggerF logger("NtQueryInformationFile", NtQueryInformationFileLogParam);

/**
 * @brief Offset of the name record inside the answer of a class.
 * @param[in] FileInformationClass Class of the call.
 * @return Offset of the `FileNameLength` field of the record.
 */
static size_t NameRecordOffset(FILE_INFORMATION_CLASS FileInformationClass)
{
    if (FileInformationClass == FileAllInformation)
    {
        return offsetof(FILE_ALL_INFORMATION, NameInformation);
    }

    return 0;
}

/**
 * @brief Answer a name carrying class with the path of the view.
 *
 * The file system reports the name of an object relative to the root of the
 * volume of its handle, so a handle the sandbox opened inside a layer is named
 * after that layer, which leaks the layout of the sandbox to the application.
 * The helper translates the name back into the name of the view, which is what
 * the application would see for the same object outside the sandbox.
 *
 * A name which belongs to no layer of the view is left as it is, so the answer
 * of a handle of the host filesystem and of an object which is not a file
 * stays byte for byte the answer of the system. A failure of the translation
 * is not reported either: the helper only ever shortens the name, and the
 * caller of a query which cannot be answered from the view keeps the answer of
 * the system.
 *
 * @param[in] FileInformationClass Class of the call.
 * @param[in,out] FileInformation Answer of the system, rewritten in place.
 * @param[in] Length Size of the answer.
 * @param[in,out] IoStatusBlock Status block of the call, may be null.
 * @return `STATUS_SUCCESS` when the answer is complete, and
 *         `STATUS_BUFFER_OVERFLOW` when the name of the view does not fit the
 *         buffer, in which case the status block reports the size it needs.
 */
static NTSTATUS AnswerNameInPlace(FILE_INFORMATION_CLASS FileInformationClass, PVOID FileInformation, ULONG Length,
                                  PIO_STATUS_BLOCK IoStatusBlock)
{
    const size_t record_offset = NameRecordOffset(FileInformationClass);
    if (FileInformation == nullptr || Length < record_offset + sizeof(ULONG))
    {
        return STATUS_SUCCESS;
    }

    auto*       name_length = reinterpret_cast<ULONG*>(reinterpret_cast<BYTE*>(FileInformation) + record_offset);
    const ULONG name_bytes = *name_length;
    if (name_bytes == 0 || (name_bytes % sizeof(WCHAR)) != 0 || record_offset + sizeof(ULONG) + name_bytes > Length)
    {
        /* A record which does not carry a name the hook may read. */
        return STATUS_SUCCESS;
    }

    const auto* name_begin =
        reinterpret_cast<const WCHAR*>(reinterpret_cast<const BYTE*>(FileInformation) + record_offset + sizeof(ULONG));
    const std::wstring layer_name(name_begin, name_bytes / sizeof(WCHAR));

    std::wstring view_name;
    if (!appbox::filesystem::RebaseLayerPathToView(layer_name, view_name))
    {
        /* The handle does not denote a layer of the view. */
        return STATUS_SUCCESS;
    }

    const ULONG view_bytes = static_cast<ULONG>(view_name.size() * sizeof(WCHAR));
    const ULONG answer_size = static_cast<ULONG>(record_offset) + sizeof(ULONG) + view_bytes;
    if (answer_size > Length)
    {
        /*
         * The path of the view does not fit the buffer of the caller, which is
         * possible because a lower layer of a view may map a deep path onto a
         * shallow one. The caller is told the size it needs instead of being
         * handed a truncated name.
         */
        if (IoStatusBlock != nullptr)
        {
            IoStatusBlock->Information = answer_size;
        }
        return STATUS_BUFFER_OVERFLOW;
    }

    *name_length = view_bytes;
    memcpy(reinterpret_cast<BYTE*>(FileInformation) + record_offset + sizeof(ULONG), view_name.c_str(), view_bytes);
    if (IoStatusBlock != nullptr)
    {
        IoStatusBlock->Information = answer_size;
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Answer a name carrying class whose answer did not fit the caller.
 *
 * The buffer of the caller is too small for the name of the layer, so the
 * query is repeated into a local buffer to learn the whole name. The answer of
 * the view is usually shorter than the one of the layer, which is why it is
 * copied into the buffer of the caller when it fits: the caller then receives
 * the name it asked for instead of a failure it cannot recover from, because
 * the file system reports the bytes it filled rather than the size the name
 * needs.
 *
 * @param[in] FileHandle Handle of the call.
 * @param[in] FileInformationClass Class of the call.
 * @param[out] FileInformation Buffer of the caller.
 * @param[in] Length Size of the buffer of the caller.
 * @param[in,out] IoStatusBlock Status block of the call, may be null.
 * @return Status of the call.
 */
static NTSTATUS AnswerNameFromLocalQuery(HANDLE FileHandle, FILE_INFORMATION_CLASS FileInformationClass,
                                         PVOID FileInformation, ULONG Length, PIO_STATUS_BLOCK IoStatusBlock)
{
    std::vector<BYTE> local(kNameBufferSize);
    IO_STATUS_BLOCK   local_status = {};
    NTSTATUS st = sys_NtQueryInformationFile(FileHandle, &local_status, local.data(), static_cast<ULONG>(local.size()),
                                             FileInformationClass);
    if (st == STATUS_BUFFER_OVERFLOW || !NT_SUCCESS(st))
    {
        /* The path is longer than the local buffer: keep the answer of the system. */
        return STATUS_BUFFER_OVERFLOW;
    }

    const NTSTATUS answer =
        AnswerNameInPlace(FileInformationClass, local.data(), static_cast<ULONG>(local.size()), &local_status);
    if (answer != STATUS_SUCCESS)
    {
        return answer;
    }

    const ULONG answer_size = static_cast<ULONG>(local_status.Information);
    if (IoStatusBlock != nullptr)
    {
        IoStatusBlock->Information = answer_size;
    }
    if (FileInformation == nullptr || Length < answer_size)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    memcpy(FileInformation, local.data(), answer_size);
    return STATUS_SUCCESS;
}

static NTSTATUS Hook_NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                            ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    logger.Log(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    if (!appbox::filesystem::QueryInformationCarriesName(FileInformationClass))
    {
        /* The class reports a property of the object, not its path. */
        return sys_NtQueryInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    }

    NTSTATUS st = sys_NtQueryInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    /*
     * A truncated answer is a warning, which `NT_SUCCESS` reports as a failure:
     * the buffer of the caller was too small for the name of the layer, while
     * the name of the view may still fit it.
     */
    if (st == STATUS_BUFFER_OVERFLOW)
    {
        return AnswerNameFromLocalQuery(FileHandle, FileInformationClass, FileInformation, Length, IoStatusBlock);
    }

    if (!NT_SUCCESS(st))
    {
        /* A failure carries no name the caller may read. */
        return st;
    }

    const NTSTATUS answer = AnswerNameInPlace(FileInformationClass, FileInformation, Length, IoStatusBlock);
    return answer == STATUS_SUCCESS ? st : answer;
}

static void LoadNtQueryInformationFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryInformationFile");
    sys_NtQueryInformationFile = reinterpret_cast<T_NtQueryInformationFile>(addr);
}

appbox::HookRecord appbox::HookNtQueryInformationFile = {
    "NtQueryInformationFile",
    LoadNtQueryInformationFile,
    (void**)&sys_NtQueryInformationFile,
    Hook_NtQueryInformationFile,
};
