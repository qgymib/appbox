#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <vector>
#include "filesystem/CreateDirectory.hpp"
#include "filesystem/DirName.hpp"
#include "filesystem/Resolve.hpp"
#include "filesystem/RemoveAll.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "utils/BitParser.hpp"
#include "utils/CopyFileNt.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "utils/Defines.hpp"
#include "__init__.hpp"
#include "WString.hpp"
#include "Sandbox.hpp"

enum class PathKind
{
    NotExist,
    Directory,
    File,
    Error // other failure (access denied, etc.); treated as not-traversable
};

T_NtCreateFile sys_NtCreateFile = nullptr;

static const appbox::BitData CreateDispositionMap[] = {
    { "FILE_OVERWRITE_IF", FILE_OVERWRITE_IF },
    { "FILE_OPEN_IF",      FILE_OPEN_IF      },
    { "FILE_CREATE",       FILE_CREATE       },
    { "FILE_OVERWRITE",    FILE_OVERWRITE    },
    { "FILE_OPEN",         FILE_OPEN         },
    { "FILE_SUPERSEDE",    FILE_SUPERSEDE    },
};

static const appbox::BitData CreateOptionsMap[] = {
    { "FILE_DIRECTORY_FILE",            FILE_DIRECTORY_FILE            },
    { "FILE_NON_DIRECTORY_FILE",        FILE_NON_DIRECTORY_FILE        },
    { "FILE_WRITE_THROUGH",             FILE_WRITE_THROUGH             },
    { "FILE_SEQUENTIAL_ONLY",           FILE_SEQUENTIAL_ONLY           },
    { "FILE_RANDOM_ACCESS",             FILE_RANDOM_ACCESS             },
    { "FILE_NO_INTERMEDIATE_BUFFERING", FILE_NO_INTERMEDIATE_BUFFERING },
    { "FILE_SYNCHRONOUS_IO_ALERT",      FILE_SYNCHRONOUS_IO_ALERT      },
    { "FILE_SYNCHRONOUS_IO_NONALERT",   FILE_SYNCHRONOUS_IO_NONALERT   },
    { "FILE_CREATE_TREE_CONNECTION",    FILE_CREATE_TREE_CONNECTION    },
    { "FILE_NO_EA_KNOWLEDGE",           FILE_NO_EA_KNOWLEDGE           },
    { "FILE_OPEN_REPARSE_POINT",        FILE_OPEN_REPARSE_POINT        },
    { "FILE_DELETE_ON_CLOSE",           FILE_DELETE_ON_CLOSE           },
    { "FILE_OPEN_BY_FILE_ID",           FILE_OPEN_BY_FILE_ID           },
    { "FILE_OPEN_FOR_BACKUP_INTENT",    FILE_OPEN_FOR_BACKUP_INTENT    },
    { "FILE_RESERVE_OPFILTER",          FILE_RESERVE_OPFILTER          },
    { "FILE_OPEN_REQUIRING_OPLOCK",     FILE_OPEN_REQUIRING_OPLOCK     },
    { "FILE_COMPLETE_IF_OPLOCKED",      FILE_COMPLETE_IF_OPLOCKED      },
};

static const appbox::BitData FileAttributesMap[] = {
    { "FILE_ATTRIBUTE_READONLY",              FILE_ATTRIBUTE_READONLY              },
    { "FILE_ATTRIBUTE_HIDDEN",                FILE_ATTRIBUTE_HIDDEN                },
    { "FILE_ATTRIBUTE_SYSTEM",                FILE_ATTRIBUTE_SYSTEM                },
    { "FILE_ATTRIBUTE_DIRECTORY",             FILE_ATTRIBUTE_DIRECTORY             },
    { "FILE_ATTRIBUTE_ARCHIVE",               FILE_ATTRIBUTE_ARCHIVE               },
    { "FILE_ATTRIBUTE_DEVICE",                FILE_ATTRIBUTE_DEVICE                },
    { "FILE_ATTRIBUTE_NORMAL",                FILE_ATTRIBUTE_NORMAL                },
    { "FILE_ATTRIBUTE_TEMPORARY",             FILE_ATTRIBUTE_TEMPORARY             },
    { "FILE_ATTRIBUTE_SPARSE_FILE",           FILE_ATTRIBUTE_SPARSE_FILE           },
    { "FILE_ATTRIBUTE_REPARSE_POINT",         FILE_ATTRIBUTE_REPARSE_POINT         },
    { "FILE_ATTRIBUTE_COMPRESSED",            FILE_ATTRIBUTE_COMPRESSED            },
    { "FILE_ATTRIBUTE_OFFLINE",               FILE_ATTRIBUTE_OFFLINE               },
    { "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED",   FILE_ATTRIBUTE_NOT_CONTENT_INDEXED   },
    { "FILE_ATTRIBUTE_ENCRYPTED",             FILE_ATTRIBUTE_ENCRYPTED             },
    { "FILE_ATTRIBUTE_INTEGRITY_STREAM",      FILE_ATTRIBUTE_INTEGRITY_STREAM      },
    { "FILE_ATTRIBUTE_VIRTUAL",               FILE_ATTRIBUTE_VIRTUAL               },
    { "FILE_ATTRIBUTE_NO_SCRUB_DATA",         FILE_ATTRIBUTE_NO_SCRUB_DATA         },
    { "FILE_ATTRIBUTE_EA",                    FILE_ATTRIBUTE_EA                    },
    { "FILE_ATTRIBUTE_PINNED",                FILE_ATTRIBUTE_PINNED                },
    { "FILE_ATTRIBUTE_UNPINNED",              FILE_ATTRIBUTE_UNPINNED              },
    { "FILE_ATTRIBUTE_RECALL_ON_OPEN",        FILE_ATTRIBUTE_RECALL_ON_OPEN        },
    { "FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS", FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS },
};

static const appbox::BitData ObjectAttributesMap[] = {
    { "OBJ_CASE_INSENSITIVE", OBJ_CASE_INSENSITIVE },
    { "OBJ_INHERIT",          OBJ_INHERIT          },
};

static nlohmann::json NtCreateFileLogParam(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
                                           PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
                                           ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    nlohmann::json json;
    json["FileHandle"] = appbox::PointerToString(FileHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes, CreateOptions);
    json["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    if (AllocationSize != nullptr)
    {
        json["AllocationSize"] = AllocationSize->QuadPart;
    }
    json["FileAttributes"] = appbox::ParseBit(FileAttributes, FileAttributesMap, std::size(FileAttributesMap));
    json["ShareAccess"] = ShareAccess;
    json["CreateDisposition"] =
        appbox::ParseBit(CreateDisposition, CreateDispositionMap, std::size(CreateDispositionMap));
    json["CreateOptions"] = appbox::ParseBit(CreateOptions, CreateOptionsMap, std::size(CreateOptionsMap));
    json["EaBuffer"] = appbox::PointerToString(EaBuffer);
    json["EaLength"] = EaLength;

    return json;
}

static appbox::LoggerF logger("NtCreateFile", NtCreateFileLogParam);

static bool QueryHandleNtName(HANDLE h, std::wstring& out)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return false;

    ULONG    needed = 0;
    NTSTATUS st = sys_NtQueryObject(h, ObjectNameInformation, nullptr, 0, &needed);
    if (needed < sizeof(OBJECT_NAME_INFORMATION))
        needed = sizeof(OBJECT_NAME_INFORMATION) + 0x400;

    std::vector<BYTE> buf(needed);
    st = sys_NtQueryObject(h, ObjectNameInformation, buf.data(), (ULONG)buf.size(), &needed);
    if (st == STATUS_INFO_LENGTH_MISMATCH || st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL)
    {
        buf.resize(needed);
        st = sys_NtQueryObject(h, ObjectNameInformation, buf.data(), (ULONG)buf.size(), &needed);
    }
    if (!NT_SUCCESS(st))
        return false;

    auto* info = reinterpret_cast<OBJECT_NAME_INFORMATION*>(buf.data());
    if (!info->Name.Buffer || info->Name.Length == 0)
        return false;

    out.assign(info->Name.Buffer, info->Name.Length / sizeof(wchar_t));
    return true;
}

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
        parent.append(child, 1, std::wstring::npos);
    else if (!parentSlash && !childSlash)
    {
        parent.push_back(L'\\');
        parent.append(child);
    }
    else
        parent.append(child);
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

static NTSTATUS NtCreateFileOpenFS(const std::wstring& path, ULONG Attributes, PHANDLE FileHandle,
                                   ACCESS_MASK DesiredAccess, PIO_STATUS_BLOCK IoStatusBlock,
                                   PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
                                   ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    HANDLE tmpHandle = nullptr;
    if (FileHandle == nullptr)
    {
        FileHandle = &tmpHandle;
    }
    IO_STATUS_BLOCK tmpIoStatusBlock;
    if (IoStatusBlock == nullptr)
    {
        IoStatusBlock = &tmpIoStatusBlock;
    }

    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);
    auto st = sys_NtCreateFile(FileHandle, DesiredAccess, &oa, IoStatusBlock, AllocationSize, FileAttributes,
                               ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    if (NT_SUCCESS(st) && FileHandle == &tmpHandle)
    {
        sys_NtClose(tmpHandle);
    }
    return st;
}

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
                                  ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer,
                                  ULONG EaLength)
{
    logger.Log(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
               CreateDisposition, CreateOptions, EaBuffer, EaLength);

    if (appbox::ThreadLocal::Get().disable_NtCreateFile_hook)
    {
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    /* Get file path in sandbox */
    std::wstring nativate_fs_nt_path;
    if (!appbox::ConvertToFullNtPath(ObjectAttributes, CreateOptions, nativate_fs_nt_path))
    {
        LOG_D("ConvertToFullNtPath failed");
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    std::wstring nativate_fs_path;
    if (!appbox::MappingAsDosNtPath(nativate_fs_nt_path, nativate_fs_path))
    {
        LOG_D(L"MappingAsDosNtPath failed: {}", nativate_fs_nt_path);
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }
    CreateOptions &= ~FILE_OPEN_BY_FILE_ID;

    /* Resolve path in sandbox. */
    auto resolve_result = appbox::filesystem::Resolve(nativate_fs_path);
    LOG_T("resolve: {}", nlohmann::json(resolve_result).dump());
    /* In all of conditions, the parent path must exist. */
    if (!resolve_result.bParentExist)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    /* Check CreateDisposition */
    if (CreateDisposition == FILE_CREATE && resolve_result.status == appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_COLLISION;
    }
    if (CreateDisposition == FILE_OVERWRITE &&
        resolve_result.status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    const bool want_create = (CreateDisposition == FILE_SUPERSEDE || CreateDisposition == FILE_CREATE ||
                              CreateDisposition == FILE_OPEN_IF || CreateDisposition == FILE_OVERWRITE_IF);
    const bool want_edit = (DesiredAccess & (DELETE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
                                             FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL));

    /* If file is hidden by whiteout, remove the whiteout file */
    if (want_create && resolve_result.status == appbox::filesystem::ResolveResult::Status::HiddenByWhiteout &&
        resolve_result.bWhiteoutInUpper)
    {
        appbox::filesystem::RemoveAll(resolve_result.whiteoutPath, ObjectAttributes->Attributes);

        /* If want to create directory, search again to check if we need to create opaque file */
        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            auto rResult = appbox::filesystem::Resolve(nativate_fs_path);
            if (rResult.status == appbox::filesystem::ResolveResult::Status::Exists)
            {
                NtCreateFileOpenFS(nativate_fs_path, ObjectAttributes->Attributes, nullptr, DesiredAccess,
                                   IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                   CreateOptions, EaBuffer, EaLength);
                return NtCreateFileOpenFS(nativate_fs_path + L"\\" + APPBOX_SANDBOX_OPAQUE_NAME_W,
                                          ObjectAttributes->Attributes, nullptr, DELETE | FILE_WRITE_DATA, nullptr,
                                          nullptr, FILE_ATTRIBUTE_NORMAL,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN_IF,
                                          FILE_NON_DIRECTORY_FILE, nullptr, 0);
            }
        }
    }
    if (want_create || want_edit)
    {
        auto dir = appbox::filesystem::DirName(resolve_result.uPath);
        appbox::filesystem::CreateDirectories(dir, resolve_result.uPathBaseSize);
    }
    if (want_edit)
    {
        if (resolve_result.status == appbox::filesystem::ResolveResult::Status::Exists && !resolve_result.bInUpper)
        {
            appbox::CopyFileNt(resolve_result.hPath[0].fPath, resolve_result.uPath);
        }
    }
    std::wstring open_path = (want_edit || resolve_result.status != appbox::filesystem::ResolveResult::Status::Exists)
                                 ? resolve_result.uPath
                                 : resolve_result.hPath[0].fPath;

    return NtCreateFileOpenFS(open_path, ObjectAttributes->Attributes, FileHandle, DesiredAccess, IoStatusBlock,
                              AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer,
                              EaLength);
}

appbox::NtCreateFileLock::NtCreateFileLock()
{
    appbox::ThreadLocal::Get().disable_NtCreateFile_hook = true;
}

appbox::NtCreateFileLock::~NtCreateFileLock()
{
    appbox::ThreadLocal::Get().disable_NtCreateFile_hook = false;
}

void appbox::AttachNtCreateFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtCreateFile");
    sys_NtCreateFile = reinterpret_cast<T_NtCreateFile>(addr);

    DetourAttach(&sys_NtCreateFile, Hook_NtCreateFile);
}

void appbox::DetachNtCreateFile()
{
    DetourDetach(&sys_NtCreateFile, Hook_NtCreateFile);
}

bool appbox::ConvertToFullNtPath(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions, std::wstring& path)
{
    path.clear();
    if (!ObjectAttributes)
        return false;

    UNICODE_STRING* objName = ObjectAttributes->ObjectName;
    HANDLE          root = ObjectAttributes->RootDirectory;

    // ---------- 场景 1：按 FileID 打开 ----------
    if (CreateOptions & FILE_OPEN_BY_FILE_ID)
    {
        if (!root || !objName || !objName->Buffer || (objName->Length != 8 && objName->Length != 16))
        {
            return false;
        }

        // 取卷/根目录的 NT 路径
        std::wstring rootPath;
        if (!QueryHandleNtName(root, rootPath))
            return false;

        // 用 FileID 再开一次以拿到真实文件名
        HANDLE            fh = nullptr;
        OBJECT_ATTRIBUTES oa{};
        InitializeObjectAttributes(&oa, objName, OBJ_CASE_INSENSITIVE, root, nullptr);
        IO_STATUS_BLOCK iosb{};
        NTSTATUS st = sys_NtOpenFile(&fh, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     FILE_OPEN_BY_FILE_ID | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT);

        if (NT_SUCCESS(st) && fh)
        {
            std::wstring real;
            bool         ok = QueryHandleNtName(fh, real);
            sys_NtClose(fh);
            if (ok)
            {
                path = std::move(real);
                return true;
            }
        }

        // 兜底：返回 <卷路径>\:fid:<hex>
        path = rootPath;
        JoinNtPath(path, FormatFileIdFallback(*objName));
        return true;
    }

    // ---------- 场景 2：普通字符串路径 ----------
    std::wstring objStr;
    if (objName && objName->Buffer && objName->Length)
    {
        objStr.assign(objName->Buffer, objName->Length / sizeof(wchar_t));
    }

    if (root == nullptr)
    {
        // 必须已经是完整 NT 路径
        if (objStr.empty() || objStr.front() != L'\\')
            return false;
        path = std::move(objStr);
        return true;
    }

    // RootDirectory 非空：相对路径，需用 NtQueryObject 取根的 NT 名
    std::wstring rootPath;
    if (!QueryHandleNtName(root, rootPath))
        return false;

    path = std::move(rootPath);
    JoinNtPath(path, objStr); // objStr 可以为空（表示就是根本身）
    return !path.empty();
}

nlohmann::json appbox::ToJson(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions)
{
    nlohmann::json json;
    json["Length"] = ObjectAttributes->Length;
    json["RootDirectory"] = appbox::PointerToString(ObjectAttributes->RootDirectory);
    if (ObjectAttributes->ObjectName != nullptr)
    {
        nlohmann::json name;
        name["Length"] = ObjectAttributes->ObjectName->Length;
        name["MaximumLength"] = ObjectAttributes->ObjectName->MaximumLength;

        if (CreateOptions & FILE_OPEN_BY_FILE_ID)
        {
            auto p_file_id = reinterpret_cast<uint64_t*>(ObjectAttributes->ObjectName->Buffer);
            name["Buffer"] = *p_file_id;
        }
        else
        {
            name["Buffer"] = appbox::WideToUTF8(ObjectAttributes->ObjectName->Buffer);
        }

        json["ObjectName"] = name;
    }
    json["Attributes"] =
        appbox::ParseBit(ObjectAttributes->Attributes, ObjectAttributesMap, std::size(ObjectAttributesMap));
    json["SecurityDescriptor"] = appbox::PointerToString(ObjectAttributes->SecurityDescriptor);
    json["SecurityQualityOfService"] = appbox::PointerToString(ObjectAttributes->SecurityQualityOfService);

    return json;
}
