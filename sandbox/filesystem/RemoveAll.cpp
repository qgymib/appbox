#include "hook/NtClose.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryInformationFile.hpp"
#include "hook/NtSetInformationFile.hpp"
#include "RemoveAll.hpp"
#include <vector>

static NTSTATUS RemoveOneRelative(HANDLE root, const std::wstring& name, ULONG attributes);

static NTSTATUS NtOpenChild(HANDLE& outHandle, HANDLE root, const std::wstring& name, ULONG attributes,
                            ACCESS_MASK access, ULONG openOptions)
{
    UNICODE_STRING us{};
    us.Length = static_cast<USHORT>(name.size() * sizeof(WCHAR));
    us.MaximumLength = us.Length;
    us.Buffer = const_cast<PWSTR>(name.c_str());

    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &us, attributes, root, nullptr);

    IO_STATUS_BLOCK iosb{};
    return sys_NtOpenFile(&outHandle, access, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          openOptions);
}

static NTSTATUS NtClearReadOnlyIfNeeded(HANDLE handle, FILE_BASIC_INFORMATION& bi)
{
    if ((bi.FileAttributes & FILE_ATTRIBUTE_READONLY) == 0)
    {
        return 0;
    }
    FILE_BASIC_INFORMATION newBi = bi;
    newBi.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
    if (newBi.FileAttributes == 0)
    {
        newBi.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    }
    // CreationTime 等设为 0 表示不修改时间戳
    newBi.CreationTime.QuadPart = 0;
    newBi.LastAccessTime.QuadPart = 0;
    newBi.LastWriteTime.QuadPart = 0;
    newBi.ChangeTime.QuadPart = 0;

    IO_STATUS_BLOCK iosb;
    NTSTATUS        s = sys_NtSetInformationFile(handle, &iosb, &newBi, sizeof(newBi), FileBasicInformation);
    if (NT_SUCCESS(s))
    {
        bi.FileAttributes = newBi.FileAttributes;
        return 0;
    }
    return s;
}

static NTSTATUS EmptyDirectory(HANDLE dirHandle, ULONG attributes)
{
    // 先把所有条目名称收集起来，避免一边迭代一边删除导致迭代器状态被破坏
    std::vector<std::wstring> entries;
    std::vector<BYTE>         buffer(8192);

    BOOLEAN restart = TRUE;
    for (;;)
    {
        IO_STATUS_BLOCK iosb{};
        NTSTATUS        s = sys_NtQueryDirectoryFile(dirHandle, nullptr, nullptr, nullptr, &iosb, buffer.data(),
                                                     static_cast<ULONG>(buffer.size()), FileDirectoryInformation, FALSE,
                                                     nullptr, restart);
        restart = FALSE;

        if (s == STATUS_NO_MORE_FILES)
            break;
        if (!NT_SUCCESS(s))
            return s;

        BYTE* p = buffer.data();
        for (;;)
        {
            auto*        info = reinterpret_cast<FILE_DIRECTORY_INFORMATION*>(p);
            std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
            if (name != L"." && name != L"..")
            {
                entries.emplace_back(std::move(name));
            }
            if (info->NextEntryOffset == 0)
                break;
            p += info->NextEntryOffset;
        }
    }

    for (const auto& entry : entries)
    {
        NTSTATUS st = RemoveOneRelative(dirHandle, entry, attributes);
        if (!NT_SUCCESS(st))
        {
            return st;
        }
    }
    return 0;
}

static NTSTATUS NtMarkForDelete(HANDLE handle)
{
    IO_STATUS_BLOCK              iosb{};
    FILE_DISPOSITION_INFORMATION di;
    di.DeleteFileOnClose = TRUE;
    NTSTATUS s = sys_NtSetInformationFile(handle, &iosb, &di, sizeof(di), FileDispositionInformation);
    return s;
}

static NTSTATUS RemoveOneRelative(HANDLE root, const std::wstring& name, ULONG attributes)
{
    // 同时申请文件和目录可能用到的访问权限：
    //  - DELETE                : 标记删除
    //  - FILE_READ_ATTRIBUTES  : 读取属性
    //  - FILE_WRITE_ATTRIBUTES : 清除只读
    //  - FILE_LIST_DIRECTORY   : 枚举目录（与 FILE_READ_DATA 同值）
    //  - SYNCHRONIZE           : 同步 IO
    constexpr ACCESS_MASK kAccess =
        DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | FILE_LIST_DIRECTORY | SYNCHRONIZE;

    // FILE_OPEN_REPARSE_POINT : 不跟随符号链接 / 重解析点
    constexpr ULONG kOpts = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT;

    HANDLE   h = nullptr;
    NTSTATUS st = NtOpenChild(h, root, name, attributes, kAccess, kOpts);
    if (!NT_SUCCESS(st))
    {
        return st;
    }

    // 查询基本信息以判断是文件还是目录、是否重解析点、是否只读
    IO_STATUS_BLOCK        iosb;
    FILE_BASIC_INFORMATION bi;
    st = sys_NtQueryInformationFile(h, &iosb, &bi, sizeof(bi), FileBasicInformation);
    if (!NT_SUCCESS(st))
    {
        sys_NtClose(h);
        return st;
    }

    const bool isDir = (bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool isReparse = (bi.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    // 清除只读属性（如果有）
    st = NtClearReadOnlyIfNeeded(h, bi);
    if (!NT_SUCCESS(st))
    {
        sys_NtClose(h);
        return st;
    }

    // 真实目录则递归清空内容；重解析点（符号链接、挂载点）当作单个对象删除
    if (isDir && !isReparse)
    {
        st = EmptyDirectory(h, attributes);
        if (!NT_SUCCESS(st))
        {
            sys_NtClose(h);
            return st;
        }
    }

    // 标记删除
    st = NtMarkForDelete(h);
    if (!NT_SUCCESS(st))
    {
        sys_NtClose(h);
        return st;
    }

    // 关闭句柄触发实际删除
    sys_NtClose(h);
    return 0;
}

NTSTATUS appbox::filesystem::RemoveAll(const std::wstring& path, ULONG Attributes)
{
    if (path.empty())
    {
        return STATUS_INVALID_PARAMETER;
    }

    return RemoveOneRelative(nullptr, path, Attributes);
}
