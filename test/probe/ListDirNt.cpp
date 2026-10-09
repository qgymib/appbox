#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include "WString.hpp"
#include "ListDirNt.hpp"

using namespace appbox::test;

/* clang-format off */
typedef NTSTATUS (*T_NtOpenFileProbe)(
    /* [OUT] */ PHANDLE             FileHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [OUT] */ PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */  ULONG               ShareAccess,
    /* [IN] */  ULONG               OpenOptions
);

typedef NTSTATUS (*T_NtQueryDirectoryFileProbe)(
    /* [IN] */              HANDLE                  FileHandle,
    /* [IN, OPTIONAL] */    HANDLE                  Event,
    /* [IN, OPTIONAL] */    PIO_APC_ROUTINE         ApcRoutine,
    /* [IN, OPTIONAL] */    PVOID                   ApcContext,
    /* [OUT] */             PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */             PVOID                   FileInformation,
    /* [IN] */              ULONG                   Length,
    /* [IN] */              FILE_INFORMATION_CLASS  FileInformationClass,
    /* [IN] */              BOOLEAN                 ReturnSingleEntry,
    /* [IN, OPTIONAL] */    PUNICODE_STRING         FileName,
    /* [IN] */              BOOLEAN                 RestartScan
);

typedef NTSTATUS (*T_NtQueryDirectoryFileExProbe)(
    /* [IN] */              HANDLE                  FileHandle,
    /* [IN, OPTIONAL] */    HANDLE                  Event,
    /* [IN, OPTIONAL] */    PIO_APC_ROUTINE         ApcRoutine,
    /* [IN, OPTIONAL] */    PVOID                   ApcContext,
    /* [OUT] */             PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */             PVOID                   FileInformation,
    /* [IN] */              ULONG                   Length,
    /* [IN] */              FILE_INFORMATION_CLASS  FileInformationClass,
    /* [IN] */              ULONG                   QueryFlags,
    /* [IN, OPTIONAL] */    PUNICODE_STRING         FileName
);
/* clang-format on */

/** Size of the buffer which receives one entry of the enumeration. */
static const ULONG kEntryBufferSize = 0x1000;

/**
 * @brief Information class of a query together with the layout of one entry.
 *
 * The classes which report a directory share the chain of their entries but
 * place the name of an entry at a different offset, so the probe needs the
 * offset of the name and the offset of its length to read an entry.
 */
struct DirectoryClass
{
    const char*            name;               /* Name of the class. */
    FILE_INFORMATION_CLASS info_class;         /* Value the query carries. */
    size_t                 name_length_offset; /* Offset of the length of the name. */
    size_t                 name_offset;        /* Offset of the name, zero when the class carries none. */
};

/**
 * @brief Value of the name offsets of a class which carries no name of an entry.
 *
 * A class of that kind reports a property of an entry which the probe cannot
 * read, so the probe reports the status of the call alone. The file system
 * refuses such a class for a directory of a volume, which is what a case
 * compares the answer with.
 */
constexpr size_t kClassWithoutName = 0;

/**
 * @brief The classes the probe knows.
 *
 * The probe keeps its own table instead of asking the sandbox, because a case
 * compares the answer with the names it created itself: a class the sandbox
 * reads with another layout than the file system wrote shows up as a failure of
 * the case, whatever the sandbox believes about the class.
 */
static const DirectoryClass s_directory_classes[] = {
    { "FileDirectoryInformation", FileDirectoryInformation, offsetof(FILE_DIRECTORY_INFORMATION, FileNameLength),
     offsetof(FILE_DIRECTORY_INFORMATION, FileName) },
    { "FileFullDirectoryInformation", FileFullDirectoryInformation, offsetof(FILE_FULL_DIR_INFORMATION, FileNameLength),
     offsetof(FILE_FULL_DIR_INFORMATION, FileName) },
    { "FileBothDirectoryInformation", FileBothDirectoryInformation, offsetof(FILE_BOTH_DIR_INFORMATION, FileNameLength),
     offsetof(FILE_BOTH_DIR_INFORMATION, FileName) },
    { "FileNamesInformation", FileNamesInformation, offsetof(FILE_NAMES_INFORMATION, FileNameLength),
     offsetof(FILE_NAMES_INFORMATION, FileName) },
    { "FileIdBothDirectoryInformation", FileIdBothDirectoryInformation,
     offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileNameLength), offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileName) },
    { "FileIdFullDirectoryInformation", FileIdFullDirectoryInformation,
     offsetof(FILE_ID_FULL_DIR_INFORMATION, FileNameLength), offsetof(FILE_ID_FULL_DIR_INFORMATION, FileName) },
    { "FileIdExtdDirectoryInformation", FileIdExtdDirectoryInformation,
     offsetof(FILE_ID_EXTD_DIR_INFORMATION, FileNameLength), offsetof(FILE_ID_EXTD_DIR_INFORMATION, FileName) },
    { "FileIdExtdBothDirectoryInformation", FileIdExtdBothDirectoryInformation,
     offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, FileNameLength),
     offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, FileName) },
    { "FileIdGlobalTxDirectoryInformation", FileIdGlobalTxDirectoryInformation,
     offsetof(FILE_ID_GLOBAL_TX_DIR_INFORMATION, FileNameLength),
     offsetof(FILE_ID_GLOBAL_TX_DIR_INFORMATION, FileName) },
    { "FileObjectIdInformation", FileObjectIdInformation, kClassWithoutName, kClassWithoutName },
    { "FileReparsePointInformation", FileReparsePointInformation, kClassWithoutName, kClassWithoutName },
};

/**
 * @brief Find the class a request names.
 * @param[in] name Name of the class, empty for the default of the probe.
 * @return The class, or null when the probe does not know the name.
 */
static const DirectoryClass* FindDirectoryClass(const std::string& name)
{
    const std::string wanted = name.empty() ? std::string("FileFullDirectoryInformation") : name;

    for (const auto& cls : s_directory_classes)
    {
        if (wanted == cls.name)
        {
            return &cls;
        }
    }

    return nullptr;
}

/**
 * @brief Read the name of the first entry of a buffer.
 * @param[in] buffer Buffer the file system filled.
 * @param[in] cls Class of the buffer.
 * @return The name of the entry.
 */
static std::wstring EntryName(const std::vector<BYTE>& buffer, const DirectoryClass& cls)
{
    ULONG length = 0;
    std::memcpy(&length, buffer.data() + cls.name_length_offset, sizeof(length));

    const auto* name = reinterpret_cast<const wchar_t*>(buffer.data() + cls.name_offset);
    return std::wstring(name, length / sizeof(wchar_t));
}

/**
 * @brief Initialize a UNICODE_STRING which refers to a wide string.
 *
 * The string is not copied: a caller has to keep \p text alive for as long as
 * the query which uses \p us runs. A temporary of the call would be released
 * before the file system reads the buffer.
 *
 * @param[out] us The string to initialize.
 * @param[in] text The text to refer to, which has to outlive \p us.
 */
static void InitString(UNICODE_STRING& us, const std::wstring& text)
{
    us.Length = static_cast<USHORT>(text.size() * sizeof(wchar_t));
    us.MaximumLength = static_cast<USHORT>(us.Length + sizeof(wchar_t));
    us.Buffer = const_cast<PWSTR>(text.c_str());
}

/**
 * @brief Open the directory a request names.
 *
 * The plain way is `NtOpenFile`, which is the entry point the sandbox registers
 * a handle with. A request which asks for `CreateFileW` takes the other way a
 * directory handle reaches the sandbox, which is the way an application which
 * enumerates a directory it opened itself uses.
 *
 * @param[in] req Request of the caller.
 * @param[in] path Win32 path of the directory.
 * @param[out] handle Handle of the directory.
 * @param[in] fn_open `NtOpenFile` of ntdll.
 * @return Status of the open, an `HRESULT` when the handle comes from
 *         `CreateFileW`.
 */
static long OpenDirectory(const ProtocolListDirNt::Req& req, const std::wstring& path, HANDLE& handle,
                          T_NtOpenFileProbe fn_open)
{
    handle = nullptr;

    if (req.create_file)
    {
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            handle = nullptr;
            return static_cast<long>(HRESULT_FROM_WIN32(::GetLastError()));
        }

        return 0;
    }

    /* The NT entry points take an NT path, while the probe receives a Win32 one. */
    std::wstring nt_path = path;
    if (nt_path.size() >= 2 && nt_path[1] == L':')
    {
        nt_path.insert(0, L"\\??\\");
    }

    UNICODE_STRING us_path;
    InitString(us_path, nt_path);

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    IO_STATUS_BLOCK iosb = {};
    return fn_open(&handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb,
                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                   FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
}

static nlohmann::json ProbeListDirNt_Entry(const nlohmann::json& data)
{
    const auto req = data.get<ProtocolListDirNt::Req>();

    ProtocolListDirNt::Rsp rsp;

    const DirectoryClass* cls = FindDirectoryClass(req.info_class);
    if (cls == nullptr)
    {
        /* A class the probe cannot read is a mistake of the case. */
        rsp.status = STATUS_INVALID_PARAMETER;
        return rsp;
    }

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        rsp.status = -1;
        return rsp;
    }

    auto fn_open = reinterpret_cast<T_NtOpenFileProbe>(GetProcAddress(ntdll, "NtOpenFile"));
    auto fn_query = reinterpret_cast<T_NtQueryDirectoryFileProbe>(GetProcAddress(ntdll, "NtQueryDirectoryFile"));
    auto fn_query_ex = reinterpret_cast<T_NtQueryDirectoryFileExProbe>(GetProcAddress(ntdll, "NtQueryDirectoryFileEx"));
    if (fn_open == nullptr || fn_query == nullptr || (req.extended && fn_query_ex == nullptr))
    {
        rsp.status = -1;
        return rsp;
    }

    std::wstring path = appbox::UTF8ToWide(req.path);
    while (!path.empty() && path.back() == L'\\')
    {
        path.pop_back();
    }

    HANDLE   handle = nullptr;
    NTSTATUS status = OpenDirectory(req, path, handle, fn_open);
    if (!NT_SUCCESS(status))
    {
        rsp.status = status;
        return rsp;
    }

    /*
     * A request which asks for a handle the sandbox did not open duplicates the
     * handle and closes the original: the duplicate denotes the same object
     * while it carries no record of the open, like the handle a process
     * inherits or duplicates from another process.
     */
    if (req.duplicate)
    {
        HANDLE copy = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            const long error = static_cast<long>(HRESULT_FROM_WIN32(::GetLastError()));
            CloseHandle(handle);
            rsp.status = error;
            return rsp;
        }

        CloseHandle(handle);
        handle = copy;
    }

    /*
     * A request which asks for a directory the view no longer holds removes it
     * before the query, which leaves the handle above as the only one which
     * denotes the object.
     */
    if (req.remove_before_query && !RemoveDirectoryW(path.c_str()))
    {
        const long error = static_cast<long>(HRESULT_FROM_WIN32(::GetLastError()));
        CloseHandle(handle);
        rsp.status = error;
        return rsp;
    }

    const std::wstring pattern = L"*";
    UNICODE_STRING     us_pattern;
    InitString(us_pattern, pattern);

    std::vector<BYTE> buffer(kEntryBufferSize);
    bool              first = true;
    for (;;)
    {
        IO_STATUS_BLOCK query_iosb = {};
        if (req.extended)
        {
            ULONG flags = SL_RETURN_SINGLE_ENTRY;
            if (first)
            {
                flags |= SL_RESTART_SCAN;
            }

            status = fn_query_ex(handle, nullptr, nullptr, nullptr, &query_iosb, buffer.data(),
                                 static_cast<ULONG>(buffer.size()), cls->info_class, flags, &us_pattern);
        }
        else
        {
            status = fn_query(handle, nullptr, nullptr, nullptr, &query_iosb, buffer.data(),
                              static_cast<ULONG>(buffer.size()), cls->info_class, TRUE, &us_pattern, first);
        }
        first = false;

        if (status == STATUS_NO_MORE_FILES)
        {
            status = 0;
            break;
        }
        if (!NT_SUCCESS(status))
        {
            break;
        }

        if (cls->name_offset == kClassWithoutName)
        {
            /*
             * The class carries no name, so there is nothing to read: the
             * answer of the probe is the status of the call alone.
             */
            break;
        }

        const std::wstring name = EntryName(buffer, *cls);
        if (name != L"." && name != L"..")
        {
            rsp.names.push_back(appbox::WideToUTF8(name));
        }
    }

    CloseHandle(handle);

    std::sort(rsp.names.begin(), rsp.names.end());
    rsp.status = status;
    return rsp;
}

appbox::test::Probe appbox::test::ProbeListDirNt("ListDirNt", ProbeListDirNt_Entry);
