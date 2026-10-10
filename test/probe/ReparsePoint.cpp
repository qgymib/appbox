#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <cstddef>
#include <string>
#include <vector>
#include "WString.hpp"
#include "ReparsePoint.hpp"

/* clang-format off */
typedef NTSTATUS (*T_NtCreateFileProbe)(
    /* [OUT] */         PHANDLE             FileHandle,
    /* [IN] */          ACCESS_MASK         DesiredAccess,
    /* [IN] */          POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [OUT] */         PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN,OPTIONAL] */ PLARGE_INTEGER      AllocationSize,
    /* [IN] */          ULONG               FileAttributes,
    /* [IN] */          ULONG               ShareAccess,
    /* [IN] */          ULONG               CreateDisposition,
    /* [IN] */          ULONG               CreateOptions,
    /* [IN,OPTIONAL] */ PVOID               EaBuffer,
    /* [IN] */          ULONG               EaLength
);

typedef NTSTATUS (*T_NtFsControlFileProbe)(
    /* [IN] */          HANDLE              FileHandle,
    /* [IN,OPTIONAL] */ HANDLE              Event,
    /* [IN,OPTIONAL] */ PIO_APC_ROUTINE     ApcRoutine,
    /* [IN,OPTIONAL] */ PVOID               ApcContext,
    /* [OUT] */         PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */          ULONG               FsControlCode,
    /* [IN,OPTIONAL] */ PVOID               InputBuffer,
    /* [IN] */          ULONG               InputBufferLength,
    /* [OUT,OPTIONAL]*/ PVOID               OutputBuffer,
    /* [IN] */          ULONG               OutputBufferLength
);

typedef NTSTATUS (*T_NtReadFileProbe)(
    /* [IN] */          HANDLE              FileHandle,
    /* [IN,OPTIONAL] */ HANDLE              Event,
    /* [IN,OPTIONAL] */ PIO_APC_ROUTINE     ApcRoutine,
    /* [IN,OPTIONAL] */ PVOID               ApcContext,
    /* [OUT] */         PIO_STATUS_BLOCK    IoStatusBlock,
    /* [OUT] */         PVOID               Buffer,
    /* [IN] */          ULONG               Length,
    /* [IN,OPTIONAL] */ PLARGE_INTEGER      ByteOffset,
    /* [IN,OPTIONAL] */ PULONG              Key
);

typedef NTSTATUS (*T_NtCloseProbe)(
    /* [IN] */  HANDLE  Handle
);

typedef VOID (*T_RtlInitUnicodeStringProbe)(
    /* [OUT] */ PUNICODE_STRING DestinationString,
    /* [IN] */  PCWSTR          SourceString
);
/* clang-format on */

/** The probe opens the object of a link without following it. */
static const ULONG kOpenLinkOptions = FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT;

/** The probe shares every object it holds like an application which only reads it. */
static const ULONG kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

/** The link has to be created before its reparse point can be written. */
static const ULONG kUnprivilegedCreate = 0x00000002;

/** The link is created for a folder. */
static const ULONG kDirectoryLink = 0x00000001;

/** Size of the header of the data of a reparse point. */
static constexpr std::size_t kReparseHeaderSize = sizeof(ULONG) + 2 * sizeof(USHORT);

/**
 * @brief The entry points the probe calls.
 */
struct ReparseApi
{
    T_NtCreateFileProbe         create_file = nullptr;
    T_NtFsControlFileProbe      fs_control = nullptr;
    T_NtReadFileProbe           read_file = nullptr;
    T_NtCloseProbe              close = nullptr;
    T_RtlInitUnicodeStringProbe init_unicode = nullptr;
};

static ReparseApi LoadApi()
{
    ReparseApi api;
    auto       ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return api;
    }

    api.create_file = reinterpret_cast<T_NtCreateFileProbe>(GetProcAddress(ntdll, "NtCreateFile"));
    api.fs_control = reinterpret_cast<T_NtFsControlFileProbe>(GetProcAddress(ntdll, "NtFsControlFile"));
    api.read_file = reinterpret_cast<T_NtReadFileProbe>(GetProcAddress(ntdll, "NtReadFile"));
    api.close = reinterpret_cast<T_NtCloseProbe>(GetProcAddress(ntdll, "NtClose"));
    api.init_unicode = reinterpret_cast<T_RtlInitUnicodeStringProbe>(GetProcAddress(ntdll, "RtlInitUnicodeString"));
    return api;
}

/**
 * @brief Convert a Win32 path into the name the entry points take.
 * @param[in] path Win32 path of the view.
 * @return Path of the object namespace.
 */
static std::wstring AsNtPath(const std::wstring& path)
{
    if (path.size() >= 2 && path[1] == L':')
    {
        return L"\\??\\" + path;
    }
    return path;
}

/**
 * @brief Open an object of the view without following it.
 * @param[in] api Entry points of the probe.
 * @param[in] path Win32 path of the view.
 * @param[in] access Access the probe asks for.
 * @param[out] handle Handle of the object.
 * @return Status of the open.
 */
static NTSTATUS OpenLink(const ReparseApi& api, const std::wstring& path, ACCESS_MASK access, HANDLE& handle)
{
    handle = nullptr;

    /* The name keeps the buffer alive for the whole call. */
    const std::wstring nt_path = AsNtPath(path);

    UNICODE_STRING name;
    api.init_unicode(&name, nt_path.c_str());

    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    IO_STATUS_BLOCK iosb = {};
    return api.create_file(&handle, access | SYNCHRONIZE, &attributes, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL, kShareAll,
                           FILE_OPEN, kOpenLinkOptions, nullptr, 0);
}

/**
 * @brief Build the data of a reparse point which names a target.
 * @param[in] tag Tag of the reparse point.
 * @param[in] substitute Name the file system follows.
 * @param[in] print Name the shell shows.
 * @param[in] flags Flags of a symbolic link.
 * @return Data of the reparse point.
 */
static std::vector<BYTE> BuildLinkData(ULONG tag, const std::wstring& substitute, const std::wstring& print,
                                       ULONG flags)
{
    const std::size_t pathBufferOffset =
        kReparseHeaderSize + 4 * sizeof(USHORT) + (tag == IO_REPARSE_TAG_SYMLINK ? sizeof(ULONG) : 0);
    const std::size_t substituteBytes = (substitute.size() + 1) * sizeof(wchar_t);
    const std::size_t printBytes = (print.size() + 1) * sizeof(wchar_t);

    std::vector<BYTE> data(pathBufferOffset + substituteBytes + printBytes, 0);
    auto*             buffer = reinterpret_cast<REPARSE_DATA_BUFFER*>(data.data());
    buffer->ReparseTag = tag;
    buffer->ReparseDataLength =
        static_cast<USHORT>((pathBufferOffset - kReparseHeaderSize) + substituteBytes + printBytes);
    buffer->Reserved = 0;

    WCHAR* path_buffer = nullptr;
    if (tag == IO_REPARSE_TAG_SYMLINK)
    {
        auto& variant = buffer->ReparseBuffer.SymbolicLinkReparseBuffer;
        variant.SubstituteNameOffset = 0;
        variant.SubstituteNameLength = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
        variant.PrintNameOffset = static_cast<USHORT>(substituteBytes);
        variant.PrintNameLength = static_cast<USHORT>(print.size() * sizeof(wchar_t));
        variant.Flags = flags;
        path_buffer = variant.PathBuffer;
    }
    else
    {
        auto& variant = buffer->ReparseBuffer.MountPointReparseBuffer;
        variant.SubstituteNameOffset = 0;
        variant.SubstituteNameLength = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
        variant.PrintNameOffset = static_cast<USHORT>(substituteBytes);
        variant.PrintNameLength = static_cast<USHORT>(print.size() * sizeof(wchar_t));
        path_buffer = variant.PathBuffer;
    }

    memcpy(path_buffer, substitute.c_str(), substituteBytes);
    memcpy(reinterpret_cast<BYTE*>(path_buffer) + substituteBytes, print.c_str(), printBytes);
    return data;
}

/**
 * @brief Read the two names of the data of a reparse point.
 * @param[in] data Data of the reparse point.
 * @param[out] item Result of the probe.
 */
static void ParseLinkData(const std::vector<BYTE>& data, appbox::test::ProtocolReparsePoint::Item& item)
{
    if (data.size() < kReparseHeaderSize + 4 * sizeof(USHORT))
    {
        return;
    }

    const auto* buffer = reinterpret_cast<const REPARSE_DATA_BUFFER*>(data.data());
    item.tag = buffer->ReparseTag;

    USHORT      substituteOffset = 0;
    USHORT      substituteLength = 0;
    USHORT      printOffset = 0;
    USHORT      printLength = 0;
    std::size_t pathBufferOffset = kReparseHeaderSize + 4 * sizeof(USHORT);
    if (buffer->ReparseTag == IO_REPARSE_TAG_SYMLINK)
    {
        if (data.size() < pathBufferOffset + sizeof(ULONG))
        {
            return;
        }
        const auto& variant = buffer->ReparseBuffer.SymbolicLinkReparseBuffer;
        substituteOffset = variant.SubstituteNameOffset;
        substituteLength = variant.SubstituteNameLength;
        printOffset = variant.PrintNameOffset;
        printLength = variant.PrintNameLength;
        pathBufferOffset += sizeof(ULONG);
    }
    else if (buffer->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
    {
        const auto& variant = buffer->ReparseBuffer.MountPointReparseBuffer;
        substituteOffset = variant.SubstituteNameOffset;
        substituteLength = variant.SubstituteNameLength;
        printOffset = variant.PrintNameOffset;
        printLength = variant.PrintNameLength;
    }
    else
    {
        return;
    }

    const std::size_t substituteBegin = pathBufferOffset + substituteOffset;
    const std::size_t printBegin = pathBufferOffset + printOffset;
    if (substituteBegin + substituteLength <= data.size() && (substituteLength % sizeof(wchar_t)) == 0)
    {
        item.substitute = appbox::WideToUTF8(std::wstring(
            reinterpret_cast<const wchar_t*>(data.data() + substituteBegin), substituteLength / sizeof(wchar_t)));
    }
    if (printBegin + printLength <= data.size() && (printLength % sizeof(wchar_t)) == 0)
    {
        item.print = appbox::WideToUTF8(
            std::wstring(reinterpret_cast<const wchar_t*>(data.data() + printBegin), printLength / sizeof(wchar_t)));
    }
}

/**
 * @brief Create the folders above a path of the view.
 * @param[in] path Win32 path of the view.
 */
static void EnsureParentDirectory(const std::wstring& path)
{
    const auto separator = path.find_last_of(L'\\');
    if (separator == std::wstring::npos || separator < 2)
    {
        return;
    }

    std::wstring parent = path.substr(0, separator);
    if (!CreateDirectoryW(parent.c_str(), nullptr))
    {
        const DWORD error = GetLastError();
        if (error == ERROR_PATH_NOT_FOUND)
        {
            EnsureParentDirectory(parent);
            CreateDirectoryW(parent.c_str(), nullptr);
        }
    }
}

/**
 * @brief Create a junction at a path of the view.
 * @param[in] api Entry points of the probe.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void CreateJunction(const ReparseApi& api, const appbox::test::ProtocolReparsePoint::Req::Item& item,
                           appbox::test::ProtocolReparsePoint::Item& result)
{
    const std::wstring path = appbox::UTF8ToWide(item.path);
    const std::wstring target = appbox::UTF8ToWide(item.target);

    EnsureParentDirectory(path);
    CreateDirectoryW(path.c_str(), nullptr);

    HANDLE handle = nullptr;
    result.status = OpenLink(api, path, GENERIC_READ | GENERIC_WRITE, handle);
    if (!NT_SUCCESS(result.status))
    {
        result.code = static_cast<DWORD>(result.status);
        return;
    }

    const std::vector<BYTE> data = BuildLinkData(IO_REPARSE_TAG_MOUNT_POINT, L"\\??\\" + target, target, 0);

    IO_STATUS_BLOCK iosb = {};
    result.status = api.fs_control(handle, nullptr, nullptr, nullptr, &iosb, FSCTL_SET_REPARSE_POINT,
                                   const_cast<BYTE*>(data.data()), static_cast<ULONG>(data.size()), nullptr, 0);
    api.close(handle);
}

/**
 * @brief Create a symbolic link at a path of the view.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void CreateSymbolicLink(const appbox::test::ProtocolReparsePoint::Req::Item& item,
                               appbox::test::ProtocolReparsePoint::Item&            result)
{
    const std::wstring path = appbox::UTF8ToWide(item.path);

    EnsureParentDirectory(path);

    DWORD flags = kUnprivilegedCreate;
    if (item.directory)
    {
        flags |= kDirectoryLink;
    }

    if (CreateSymbolicLinkW(path.c_str(), appbox::UTF8ToWide(item.target).c_str(), flags))
    {
        result.status = STATUS_SUCCESS;
        return;
    }

    result.code = GetLastError();
    result.status = STATUS_UNSUCCESSFUL;
}

/**
 * @brief Create an object which carries a reparse point of a tag of its own.
 *
 * The tag describes the entry itself instead of naming a target, so the data
 * carries no name: the probe writes a record which a case uses to pin that the
 * view leaves such an entry alone.
 *
 * @param[in] api Entry points of the probe.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void CreateTaggedObject(const ReparseApi& api, const appbox::test::ProtocolReparsePoint::Req::Item& item,
                               appbox::test::ProtocolReparsePoint::Item& result)
{
    const std::wstring path = appbox::UTF8ToWide(item.path);
    EnsureParentDirectory(path);

    if (item.directory)
    {
        CreateDirectoryW(path.c_str(), nullptr);
    }
    else
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, kShareAll, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            result.code = GetLastError();
            result.status = STATUS_UNSUCCESSFUL;
            return;
        }
        CloseHandle(file);
    }

    HANDLE handle = nullptr;
    result.status = OpenLink(api, path, GENERIC_READ | GENERIC_WRITE, handle);
    if (!NT_SUCCESS(result.status))
    {
        return;
    }

    std::vector<BYTE> data(kReparseHeaderSize + 4 * sizeof(USHORT), 0);
    auto*             buffer = reinterpret_cast<REPARSE_DATA_BUFFER*>(data.data());
    buffer->ReparseTag = item.tag;
    buffer->ReparseDataLength = static_cast<USHORT>(4 * sizeof(USHORT));

    IO_STATUS_BLOCK iosb = {};
    result.status = api.fs_control(handle, nullptr, nullptr, nullptr, &iosb, FSCTL_SET_REPARSE_POINT, data.data(),
                                   static_cast<ULONG>(data.size()), nullptr, 0);
    api.close(handle);
}

/**
 * @brief Remove the data of a reparse point.
 * @param[in] api Entry points of the probe.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void RemoveReparsePoint(const ReparseApi& api, const appbox::test::ProtocolReparsePoint::Req::Item& item,
                               appbox::test::ProtocolReparsePoint::Item& result)
{
    HANDLE handle = nullptr;
    result.status = OpenLink(api, appbox::UTF8ToWide(item.path), GENERIC_READ | GENERIC_WRITE, handle);
    if (!NT_SUCCESS(result.status))
    {
        return;
    }

    std::vector<BYTE> data(kReparseHeaderSize, 0);
    auto*             buffer = reinterpret_cast<REPARSE_DATA_BUFFER*>(data.data());
    buffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer->ReparseDataLength = 0;

    IO_STATUS_BLOCK iosb = {};
    result.status = api.fs_control(handle, nullptr, nullptr, nullptr, &iosb, FSCTL_DELETE_REPARSE_POINT, data.data(),
                                   static_cast<ULONG>(data.size()), nullptr, 0);
    api.close(handle);
}

/**
 * @brief Read the data of a reparse point.
 * @param[in] api Entry points of the probe.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void ReadReparsePoint(const ReparseApi& api, const appbox::test::ProtocolReparsePoint::Req::Item& item,
                             appbox::test::ProtocolReparsePoint::Item& result)
{
    HANDLE handle = nullptr;
    result.status = OpenLink(api, appbox::UTF8ToWide(item.path), GENERIC_READ, handle);
    if (!NT_SUCCESS(result.status))
    {
        return;
    }

    std::vector<BYTE> data(MAXIMUM_REPARSE_DATA_BUFFER_SIZE, 0);
    IO_STATUS_BLOCK   iosb = {};
    result.status = api.fs_control(handle, nullptr, nullptr, nullptr, &iosb, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                                   data.data(), static_cast<ULONG>(data.size()));
    api.close(handle);

    if (result.status == STATUS_BUFFER_OVERFLOW && iosb.Information > 0 && iosb.Information <= data.size())
    {
        result.status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(result.status))
    {
        return;
    }

    data.resize(iosb.Information);
    ParseLinkData(data, result);
}

/**
 * @brief Read the attributes of a path of the view.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void ReadAttributes(const appbox::test::ProtocolReparsePoint::Req::Item& item,
                           appbox::test::ProtocolReparsePoint::Item&            result)
{
    const std::wstring path = appbox::UTF8ToWide(item.path);
    result.attributes = GetFileAttributesW(path.c_str());
    result.code = result.attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    result.status = result.attributes == INVALID_FILE_ATTRIBUTES ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_SUCCESS;
}

/**
 * @brief Read the attributes of a path of the view through the second entry point.
 *
 * The query of the full attributes reaches `NtQueryFullAttributesFile`, which
 * is the other entry point a caller asks for the attributes with.
 *
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void ReadFullAttributes(const appbox::test::ProtocolReparsePoint::Req::Item& item,
                               appbox::test::ProtocolReparsePoint::Item&            result)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (GetFileAttributesExW(appbox::UTF8ToWide(item.path).c_str(), GetFileExInfoStandard, &data))
    {
        result.attributes = data.dwFileAttributes;
        result.status = STATUS_SUCCESS;
        return;
    }

    result.code = GetLastError();
    result.status = STATUS_OBJECT_NAME_NOT_FOUND;
}

/**
 * @brief Read the content of a file of the view.
 * @param[in] api Entry points of the probe.
 * @param[in] item Request of the probe.
 * @param[out] result Result of the probe.
 */
static void ReadText(const ReparseApi& api, const appbox::test::ProtocolReparsePoint::Req::Item& item,
                     appbox::test::ProtocolReparsePoint::Item& result)
{
    /* The name keeps the buffer alive for the whole call. */
    const std::wstring nt_path = AsNtPath(appbox::UTF8ToWide(item.path));

    UNICODE_STRING name;
    api.init_unicode(&name, nt_path.c_str());

    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE          handle = nullptr;
    IO_STATUS_BLOCK iosb = {};
    result.status =
        api.create_file(&handle, GENERIC_READ | SYNCHRONIZE, &attributes, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                        kShareAll, FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (!NT_SUCCESS(result.status))
    {
        return;
    }

    for (;;)
    {
        char            buffer[512] = {};
        IO_STATUS_BLOCK read_iosb = {};
        const NTSTATUS  status = api.read_file(handle, nullptr, nullptr, nullptr, &read_iosb, buffer,
                                               static_cast<ULONG>(sizeof(buffer)), nullptr, nullptr);
        if (status == STATUS_END_OF_FILE)
        {
            break;
        }
        if (!NT_SUCCESS(status))
        {
            result.status = status;
            break;
        }

        const auto read = static_cast<std::size_t>(read_iosb.Information);
        if (read == 0)
        {
            break;
        }
        result.text.append(buffer, read);
    }

    api.close(handle);
}

static nlohmann::json ProbeReparsePoint_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolReparsePoint::Req>();
    const auto api = LoadApi();

    appbox::test::ProtocolReparsePoint::Rsp rsp;

    for (const auto& item : req.items)
    {
        appbox::test::ProtocolReparsePoint::Item result;

        if (api.create_file == nullptr || api.fs_control == nullptr || api.read_file == nullptr ||
            api.close == nullptr || api.init_unicode == nullptr)
        {
            result.status = static_cast<long>(STATUS_PROCEDURE_NOT_FOUND);
            rsp.items.push_back(result);
            continue;
        }

        if (item.action == "junction")
        {
            CreateJunction(api, item, result);
        }
        else if (item.action == "symlink")
        {
            CreateSymbolicLink(item, result);
        }
        else if (item.action == "set_tag")
        {
            CreateTaggedObject(api, item, result);
        }
        else if (item.action == "remove")
        {
            RemoveReparsePoint(api, item, result);
        }
        else if (item.action == "read")
        {
            ReadReparsePoint(api, item, result);
        }
        else if (item.action == "attributes")
        {
            ReadAttributes(item, result);
        }
        else if (item.action == "full_attributes")
        {
            ReadFullAttributes(item, result);
        }
        else if (item.action == "read_text")
        {
            ReadText(api, item, result);
        }
        else if (item.action == "remove_dir")
        {
            if (RemoveDirectoryW(appbox::UTF8ToWide(item.path).c_str()))
            {
                result.status = STATUS_SUCCESS;
            }
            else
            {
                result.code = GetLastError();
                result.status = STATUS_UNSUCCESSFUL;
            }
        }
        else
        {
            result.status = static_cast<long>(STATUS_INVALID_PARAMETER);
        }

        rsp.items.push_back(result);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeReparsePoint("ReparsePoint", ProbeReparsePoint_Entry);
