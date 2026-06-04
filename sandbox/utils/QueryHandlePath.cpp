#include "hook/NtQueryObject.hpp"
#include "QueryHandlePath.hpp"
#include <vector>

NTSTATUS appbox::QueryHandlePath(HANDLE hFile, std::wstring& path)
{
    if (hFile == nullptr || hFile == INVALID_HANDLE_VALUE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG    needed = 0;
    NTSTATUS st = sys_NtQueryObject(hFile, ObjectNameInformation, nullptr, 0, &needed);
    if (needed < sizeof(OBJECT_NAME_INFORMATION))
    {
        needed = sizeof(OBJECT_NAME_INFORMATION) + 0x400;
    }

    std::vector<BYTE> buf(needed);
    st = sys_NtQueryObject(hFile, ObjectNameInformation, buf.data(), (ULONG)buf.size(), &needed);
    if (st == STATUS_INFO_LENGTH_MISMATCH || st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL)
    {
        buf.resize(needed);
        st = sys_NtQueryObject(hFile, ObjectNameInformation, buf.data(), (ULONG)buf.size(), &needed);
    }
    if (!NT_SUCCESS(st))
    {
        return st;
    }

    auto* info = reinterpret_cast<OBJECT_NAME_INFORMATION*>(buf.data());
    if (info->Name.Buffer == nullptr || info->Name.Length == 0)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    path.assign(info->Name.Buffer, info->Name.Length / sizeof(wchar_t));
    return STATUS_SUCCESS;
}
