#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include <vector>
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
#include "utils/MappingAsSandboxNtPath.hpp"
#include "utils/CheckFileExist.hpp"
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

static const appbox::BitData DesiredAccessMap[] = {
    /* Combination flags always go first */
    { "FILE_GENERIC_WRITE",    FILE_GENERIC_WRITE    },
    { "FILE_GENERIC_READ",     FILE_GENERIC_READ     },
    { "FILE_GENERIC_EXECUTE",  FILE_GENERIC_EXECUTE  },
    /* Individual flags */
    { "DELETE",                DELETE                },
    { "FILE_READ_DATA",        FILE_READ_DATA        },
    { "FILE_READ_ATTRIBUTES",  FILE_READ_ATTRIBUTES  },
    { "FILE_READ_EA",          FILE_READ_EA          },
    { "READ_CONTROL",          READ_CONTROL          },
    { "FILE_WRITE_DATA",       FILE_WRITE_DATA       },
    { "FILE_WRITE_ATTRIBUTES", FILE_WRITE_ATTRIBUTES },
    { "FILE_WRITE_EA",         FILE_WRITE_EA         },
    { "FILE_APPEND_DATA",      FILE_APPEND_DATA      },
    { "WRITE_DAC",             WRITE_DAC             },
    { "WRITE_OWNER",           WRITE_OWNER           },
    { "SYNCHRONIZE",           SYNCHRONIZE           },
    { "FILE_EXECUTE",          FILE_EXECUTE          },
    { "GENERIC_READ",          GENERIC_READ          },
    { "GENERIC_WRITE",         GENERIC_WRITE         },
};

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
    json["DesiredAccess"] = appbox::ParseBit(DesiredAccess, DesiredAccessMap, std::size(DesiredAccessMap));
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

static bool ConvertToFullNtPath(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions, std::wstring& path)
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

static NTSTATUS NtCreateFileOpenFS(const std::wstring& path, ULONG Attributes, PHANDLE FileHandle,
                                   ACCESS_MASK DesiredAccess, PIO_STATUS_BLOCK IoStatusBlock,
                                   PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
                                   ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);
    return sys_NtCreateFile(FileHandle, DesiredAccess, &oa, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
                            CreateDisposition, CreateOptions & ~FILE_OPEN_BY_FILE_ID, EaBuffer, EaLength);
}

// Query a single NT path and tell whether it exists and is a directory.
PathKind QueryPathKind(const std::wstring& nt_path)
{
    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, const_cast<PWSTR>(nt_path.c_str()));

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    FILE_NETWORK_OPEN_INFORMATION info = {};
    NTSTATUS                      status = sys_NtQueryFullAttributesFile(&oa, &info);

    if (NT_SUCCESS(status))
    {
        return (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? PathKind::Directory : PathKind::File;
    }
    if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND ||
        status == STATUS_NO_SUCH_FILE || status == STATUS_NOT_A_DIRECTORY)
    {
        return PathKind::NotExist;
    }
    return PathKind::Error;
}

// Resolve a path under the merged sandbox view: overlay -> base -> host.
// Returns the kind of the topmost layer that has the entry.
PathKind QueryMergedPathKind(const std::wstring& orig_path, const std::wstring& base_fs, const std::wstring& overlay_fs)
{
    std::wstring mapped;

    // 1) overlay layer (highest priority)
    if (appbox::MappingPathInSandbox(orig_path, overlay_fs, mapped))
    {
        PathKind k = QueryPathKind(mapped);
        if (k != PathKind::NotExist)
            return k;
    }
    // 2) base layer
    if (appbox::MappingPathInSandbox(orig_path, base_fs, mapped))
    {
        PathKind k = QueryPathKind(mapped);
        if (k != PathKind::NotExist)
            return k;
    }
    // 3) host layer (raw original path)
    return QueryPathKind(orig_path);
}

// Create one directory at `nt_path`. Succeeds if it already exists as a directory;
// fails (returns STATUS_OBJECT_NAME_COLLISION-ish status) if a file already occupies it.
NTSTATUS CreateOneDirectory(const std::wstring& nt_path)
{
    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, const_cast<PWSTR>(nt_path.c_str()));

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE          h = nullptr;
    IO_STATUS_BLOCK iosb = {};
    NTSTATUS status = sys_NtCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, nullptr,
                                       FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       FILE_OPEN_IF, // open or create
                                       FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);

    if (NT_SUCCESS(status) && h)
    {
        sys_NtClose(h);
    }
    return status;
}

static NTSTATUS NtCreateFileCheckParent(const std::wstring& orig_path, const std::wstring& base_fs,
                                        const std::wstring& overlay_fs)
{
    // ---- 1. Derive the parent path of orig_path. ----
    // orig_path is an NT path like "\??\D:\foo\bar". The parent is the substring
    // preceding the last backslash. We require the path to have at least one
    // component below the volume root (e.g. "\??\D:\x"); otherwise it is malformed
    // for our purposes.
    constexpr size_t kNtPrefixLen = 4; // length of "\??\"
    if (orig_path.length() <= kNtPrefixLen || orig_path.compare(0, kNtPrefixLen, L"\\??\\") != 0)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    size_t last_sep = orig_path.find_last_of(L'\\');
    if (last_sep == std::wstring::npos || last_sep < kNtPrefixLen)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }
    std::wstring parent = orig_path.substr(0, last_sep);

    // ---- 2. Walk every prefix of `parent` in the merged view, from just below
    // the volume root down to `parent` itself, ensuring each level exists and
    // is a directory. ----
    //
    // The volume root component itself ("\??\D:") is not checked here; the
    // caller is expected to have validated the volume already.
    size_t volume_end = parent.find(L'\\', kNtPrefixLen); // first '\' after "\??\X:"
    if (volume_end != std::wstring::npos)
    {
        size_t p = volume_end;
        while (p < parent.length())
        {
            size_t       next = parent.find(L'\\', p + 1);
            std::wstring component = (next == std::wstring::npos) ? parent : parent.substr(0, next);

            PathKind kind = QueryMergedPathKind(component, base_fs, overlay_fs);
            // Both "missing" and "is a file" collapse to STATUS_OBJECT_PATH_NOT_FOUND
            // per the spec. Errors are treated conservatively the same way.
            if (kind != PathKind::Directory)
            {
                return STATUS_OBJECT_PATH_NOT_FOUND;
            }

            if (next == std::wstring::npos)
                break;
            p = next;
        }
    }

    // ---- 3. Parent exists in the merged view. Mirror the parent directory
    // chain into overlay_fs so that subsequent writes can land in overlay. ----
    std::wstring overlay_parent;
    if (!appbox::MappingPathInSandbox(parent, overlay_fs, overlay_parent))
    {
        return STATUS_INVALID_PARAMETER;
    }

    // Sanity check: mapped path must live under overlay_fs.
    if (overlay_parent.length() < overlay_fs.length() ||
        overlay_parent.compare(0, overlay_fs.length(), overlay_fs) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // Walk from just inside overlay_fs down to overlay_parent, creating every
    // intermediate directory. FILE_OPEN_IF makes this idempotent if a directory
    // already exists. We assume overlay_fs itself already exists.
    size_t p = overlay_fs.length();
    while (p < overlay_parent.length())
    {
        // Ensure we step over the leading '\' of the next component.
        if (overlay_parent[p] != L'\\')
        {
            // Defensive: should not happen given consistent mapping output.
            ++p;
            continue;
        }
        size_t       next = overlay_parent.find(L'\\', p + 1);
        std::wstring sub = (next == std::wstring::npos) ? overlay_parent : overlay_parent.substr(0, next);

        NTSTATUS s = CreateOneDirectory(sub);
        if (!NT_SUCCESS(s))
        {
            // If a non-directory entry already occupies this path in overlay
            // (e.g. left over from a previous run), surface the error.
            return s;
        }

        if (next == std::wstring::npos)
            break;
        p = next;
    }

    return STATUS_SUCCESS;
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
    if (!ConvertToFullNtPath(ObjectAttributes, CreateOptions, nativate_fs_nt_path))
    {
        LOG_W("ConvertToFullNtPath failed");
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    std::wstring nativate_fs_path;
    if (!appbox::MappingAsDosNtPath(nativate_fs_nt_path, nativate_fs_path))
    {
        LOG_W(L"MappingAsDosNtPath failed: {}", nativate_fs_nt_path);
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }
    LOG_D(L"nativate_fs_path: {}", nativate_fs_path);

    /*
     * If file exists in overlay fs, open it
     */
    auto         w_overlay_fs = appbox::UTF8ToWide(appbox::sandbox->inject_data.overlay_nt_path);
    std::wstring file_path_in_overlay_fs;
    if (!appbox::MappingPathInSandbox(nativate_fs_path, w_overlay_fs, file_path_in_overlay_fs))
    {
        LOG_W("MappingPathInSandbox() for overlay_fs failed");
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }
    LOG_D(L"file_path_in_overlay_fs: {}", file_path_in_overlay_fs);
    if (appbox::CheckFileExist(file_path_in_overlay_fs))
    {
        return NtCreateFileOpenFS(file_path_in_overlay_fs, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                                  IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                  CreateOptions, EaBuffer, EaLength);
    }

    /*
     * IF file exist in base fs:
     * 1. If file is writable, copy it to overlay fs and open it
     * 2. Otherwise, open it in base fs
     */
    auto         w_base_fs = appbox::UTF8ToWide(appbox::sandbox->inject_data.base_nt_path);
    std::wstring file_path_in_base_fs;
    if (!appbox::MappingPathInSandbox(nativate_fs_path, w_base_fs, file_path_in_base_fs))
    {
        LOG_W("MappingPathInSandbox() for base_fs failed");
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }
    LOG_D(L"file_path_in_base_fs: {}", file_path_in_base_fs);
    if (appbox::CheckFileExist(file_path_in_base_fs))
    {
        if (DesiredAccess & GENERIC_WRITE)
        { /* Copy to overlay fs and open it */
            appbox::CopyFileNt(file_path_in_base_fs, file_path_in_overlay_fs);
            return NtCreateFileOpenFS(file_path_in_overlay_fs, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                                      IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                      CreateOptions, EaBuffer, EaLength);
        }
        /* Open it directly */
        return NtCreateFileOpenFS(file_path_in_base_fs, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                                  IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                  CreateOptions, EaBuffer, EaLength);
    }

    /*
     * If file exist in nativate fs:
     * 1. If file is writable, copy it to overlay fs and open it
     * 2. Otherwise, open it directly
     */
    if (appbox::CheckFileExist(nativate_fs_path))
    {
        if (DesiredAccess & GENERIC_WRITE)
        {
            appbox::CopyFileNt(nativate_fs_path, file_path_in_overlay_fs);
            return NtCreateFileOpenFS(file_path_in_overlay_fs, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                                      IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                      CreateOptions, EaBuffer, EaLength);
        }
        /* Open it directly */
        return NtCreateFileOpenFS(nativate_fs_path, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                                  IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                  CreateOptions, EaBuffer, EaLength);
    }

    /* File not exist, try to open it in overlay fs */
    LOG_D("File not exist in nativate fs, try to open it in overlay fs");

    auto nt = NtCreateFileCheckParent(nativate_fs_path, w_base_fs, w_overlay_fs);
    if (nt != 0)
    {
        return nt;
    }

    return NtCreateFileOpenFS(file_path_in_overlay_fs, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                              IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                              CreateOptions, EaBuffer, EaLength);
}

appbox::NtCreateFileLock::NtCreateFileLock()
{
    appbox::ThreadLocal::Get().disable_NtCreateFile_hook = true;
}

appbox::NtCreateFileLock::~NtCreateFileLock()
{
    appbox::ThreadLocal::Get().disable_NtCreateFile_hook = false;
}

void appbox::InjectNtCreateFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtCreateFile");
    sys_NtCreateFile = reinterpret_cast<T_NtCreateFile>(addr);

    DetourAttach(&sys_NtCreateFile, Hook_NtCreateFile);
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
