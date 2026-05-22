#include "hook/NtClose.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "CheckFileExist.hpp"

bool appbox::CheckFileExist(const std::wstring& path)
{
    HANDLE            hFile;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    UNICODE_STRING    usPath;

    sys_RtlInitUnicodeString(&usPath, path.c_str());
    InitializeObjectAttributes(&oa, &usPath, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    NTSTATUS status = sys_NtOpenFile(&hFile, FILE_READ_ATTRIBUTES, &oa, &iosb,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_NON_DIRECTORY_FILE);

    if (NT_SUCCESS(status))
    {
        // 文件存在
        sys_NtClose(hFile);
        return true;
    }
    return false;
}
