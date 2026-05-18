#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <vector>
#include "utils/Log.hpp"
#include "__init__.hpp"
#include "NtCreateFile.hpp"
#include "NtQueryObject.hpp"

T_NtCreateFile sys_NtCreateFile = nullptr;

#if 0
/**
 * @brief Get NT path by handle
 * @param[in] handle Handle to query
 * @param[out] path NT path
 * @return Query result
 */
static NTSTATUS GetNtPathByHandle(HANDLE handle, std::wstring& path)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG    returnLength = 0;
    NTSTATUS status = sys_NtQueryObject(handle, ObjectNameInformation, nullptr, 0, &returnLength);
    if (returnLength == 0)
    {
        return status;
    }

    std::vector<uint8_t> buffer;
    do
    {
        buffer.resize(returnLength + sizeof(WCHAR));
        status = sys_NtQueryObject(handle, ObjectNameInformation, buffer.data(), static_cast<ULONG>(buffer.size()),
                                   &returnLength);
    } while (status == STATUS_INFO_LENGTH_MISMATCH || status == STATUS_BUFFER_TOO_SMALL ||
             status == STATUS_BUFFER_OVERFLOW);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    auto objectNameInfo = reinterpret_cast<POBJECT_NAME_INFORMATION>(buffer.data());
    if (objectNameInfo->Name.Buffer == nullptr || objectNameInfo->Name.Length == 0)
    {
        path.clear();
        return 0;
    }

    path.assign(objectNameInfo->Name.Buffer, objectNameInfo->Name.Length / sizeof(WCHAR));
    return 0;
}
#endif

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
                                  ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer,
                                  ULONG EaLength)
{
    if (ObjectAttributes->RootDirectory != nullptr)
    {
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
                            ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

void appbox::InjectNtCreateFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtCreateFile");
    sys_NtCreateFile = reinterpret_cast<T_NtCreateFile>(addr);

    auto ret = DetourAttach(&sys_NtCreateFile, Hook_NtCreateFile);
    if (ret != NO_ERROR)
    {
        THROW_LOG("DetourAttach(NtCreateFile) failed");
    }
}
