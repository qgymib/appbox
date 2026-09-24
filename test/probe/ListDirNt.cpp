#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <algorithm>
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

/**
 * @brief Initialize a UNICODE_STRING which refers to a wide string.
 * @param[out] us The string to initialize.
 * @param[in] text The text to refer to, which has to outlive \p us.
 */
static void InitString(UNICODE_STRING& us, const std::wstring& text)
{
    us.Length = static_cast<USHORT>(text.size() * sizeof(wchar_t));
    us.MaximumLength = static_cast<USHORT>(us.Length + sizeof(wchar_t));
    us.Buffer = const_cast<PWSTR>(text.c_str());
}

static nlohmann::json ProbeListDirNt_Entry(const nlohmann::json& data)
{
    const auto req = data.get<ProtocolListDirNt::Req>();

    ProtocolListDirNt::Rsp rsp;

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        rsp.status = -1;
        return rsp;
    }

    auto fn_open = reinterpret_cast<T_NtOpenFileProbe>(GetProcAddress(ntdll, "NtOpenFile"));
    auto fn_query = reinterpret_cast<T_NtQueryDirectoryFileProbe>(GetProcAddress(ntdll, "NtQueryDirectoryFile"));
    auto fn_query_ex =
        reinterpret_cast<T_NtQueryDirectoryFileExProbe>(GetProcAddress(ntdll, "NtQueryDirectoryFileEx"));
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

    /* The NT entry points take an NT path, while the probe receives a Win32 one. */
    if (path.size() >= 2 && path[1] == L':')
    {
        path.insert(0, L"\\??\\");
    }

    UNICODE_STRING us_path;
    InitString(us_path, path);

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    IO_STATUS_BLOCK iosb = {};
    HANDLE          handle = nullptr;
    NTSTATUS        status = fn_open(&handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(status))
    {
        rsp.status = status;
        return rsp;
    }

    UNICODE_STRING us_pattern;
    InitString(us_pattern, L"*");

    std::vector<BYTE> buffer(sizeof(FILE_FULL_DIR_INFORMATION) + 0x400);
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
                                 static_cast<ULONG>(buffer.size()), FileFullDirectoryInformation, flags, &us_pattern);
        }
        else
        {
            status = fn_query(handle, nullptr, nullptr, nullptr, &query_iosb, buffer.data(),
                              static_cast<ULONG>(buffer.size()), FileFullDirectoryInformation, TRUE, &us_pattern,
                              first);
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

        const auto* info = reinterpret_cast<const FILE_FULL_DIR_INFORMATION*>(buffer.data());
        const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
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
