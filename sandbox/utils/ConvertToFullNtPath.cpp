#include "hook/NtClose.hpp"
#include "hook/NtOpenFile.hpp"
#include "utils/QueryHandlePath.hpp"
#include "ConvertToFullNtPath.hpp"

// 拼接父路径与子路径，规范化分隔符
static void JoinNtPath(std::wstring& parent, const std::wstring& child)
{
    if (child.empty())
        return;
    if (parent.empty())
    {
        parent = child;
        return;
    }

    bool parentSlash = parent.back() == L'\\';
    bool childSlash = child.front() == L'\\';

    if (parentSlash && childSlash)
    {
        parent.append(child, 1, std::wstring::npos);
    }
    else if (!parentSlash && !childSlash)
    {
        parent.push_back(L'\\');
        parent.append(child);
    }
    else
    {
        parent.append(child);
    }
}

// 把 FileID（8/16 字节二进制）格式化为可读字符串作为兜底
static std::wstring FormatFileIdFallback(const UNICODE_STRING& id)
{
    wchar_t buf[80] = { 0 };
    if (id.Length == sizeof(LONGLONG))
    {
        ULONGLONG v = *reinterpret_cast<const ULONGLONG*>(id.Buffer);
        swprintf_s(buf, L"\\:fid:0x%016llx", v);
    }
    else if (id.Length == 16)
    {
        const ULONGLONG* p = reinterpret_cast<const ULONGLONG*>(id.Buffer);
        swprintf_s(buf, L"\\:fid:0x%016llx%016llx", p[1], p[0]);
    }
    else
    {
        swprintf_s(buf, L"\\:fid:<len=%u>", id.Length);
    }
    return buf;
}

static NTSTATUS ConvertToNtPathWithFileID(const POBJECT_ATTRIBUTES ObjectAttributes, std::wstring& path)
{
    NTSTATUS        st;
    UNICODE_STRING* objName = ObjectAttributes->ObjectName;
    HANDLE          root = ObjectAttributes->RootDirectory;
    if (!root || !objName || !objName->Buffer || (objName->Length != 8 && objName->Length != 16))
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 取卷/根目录的 NT 路径
    std::wstring rootPath;
    st = appbox::QueryHandlePath(root, rootPath);
    if (!NT_SUCCESS(st))
    {
        return st;
    }

    /* Open with FileID again to get real filename. */
    HANDLE            fh = nullptr;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, objName, OBJ_CASE_INSENSITIVE, root, nullptr);
    IO_STATUS_BLOCK iosb;
    st = sys_NtOpenFile(&fh, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_OPEN_BY_FILE_ID | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT);

    if (NT_SUCCESS(st) && fh)
    {
        std::wstring real;
        auto         ok = appbox::QueryHandlePath(fh, real);
        sys_NtClose(fh);
        if (NT_SUCCESS(ok))
        {
            path = std::move(real);
            return st;
        }
    }

    // 兜底：返回 <卷路径>\:fid:<hex>
    path = rootPath;
    JoinNtPath(path, FormatFileIdFallback(*objName));
    return st;
}

NTSTATUS appbox::ConvertToFullNtPath(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions, std::wstring& path)
{
    NTSTATUS st;
    if (ObjectAttributes == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Open by FILEI_ID */
    if (CreateOptions & FILE_OPEN_BY_FILE_ID)
    {
        return ConvertToNtPathWithFileID(ObjectAttributes, path);
    }

    UNICODE_STRING* objName = ObjectAttributes->ObjectName;

    // ---------- 场景 2：普通字符串路径 ----------
    std::wstring objStr;
    if (objName && objName->Buffer && objName->Length)
    {
        objStr.assign(objName->Buffer, objName->Length / sizeof(wchar_t));
    }

    if (ObjectAttributes->RootDirectory == nullptr)
    {
        // 必须已经是完整 NT 路径
        if (objStr.empty() || objStr.front() != L'\\')
        {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        path = std::move(objStr);
        return 0;
    }

    // RootDirectory 非空：相对路径，需用 NtQueryObject 取根的 NT 名
    std::wstring rootPath;
    st = appbox::QueryHandlePath(ObjectAttributes->RootDirectory, rootPath);
    if (!NT_SUCCESS(st))
    {
        return st;
    }

    path = std::move(rootPath);
    JoinNtPath(path, objStr); // objStr 可以为空（表示就是根本身）
    return path.empty() ? STATUS_OBJECT_NAME_NOT_FOUND : 0;
}
