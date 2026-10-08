#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <cstddef>
#include <string>
#include <vector>
#include "WString.hpp"
#include "QueryInformationFile.hpp"

/* clang-format off */
typedef NTSTATUS (*T_NtQueryInformationFileProbe)(
    /* [IN] */  HANDLE                  FileHandle,
    /* [OUT] */ PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */ PVOID                   FileInformation,
    /* [IN] */  ULONG                   Length,
    /* [IN] */  FILE_INFORMATION_CLASS  FileInformationClass
);
/* clang-format on */

/** Size of the buffer which holds the answer of a name class. */
static const ULONG kNameBufferSize = 0x1000;

/**
 * @brief Read the name out of the answer of a name class.
 * @param[in] info_class Class of the query.
 * @param[in] buffer Answer of the file system.
 * @param[in] length Size of the buffer the query was asked with.
 * @return The name the answer carries.
 */
static std::wstring NameOfAnswer(FILE_INFORMATION_CLASS info_class, const std::vector<BYTE>& buffer, ULONG length)
{
    const size_t record_offset = info_class == FileAllInformation ? offsetof(FILE_ALL_INFORMATION, NameInformation) : 0;
    if (length < record_offset + sizeof(ULONG))
    {
        return L"";
    }

    const ULONG name_bytes = *reinterpret_cast<const ULONG*>(buffer.data() + record_offset);
    if (name_bytes == 0 || record_offset + sizeof(ULONG) + name_bytes > length)
    {
        return L"";
    }

    return std::wstring(reinterpret_cast<const wchar_t*>(buffer.data() + record_offset + sizeof(ULONG)),
                        name_bytes / sizeof(wchar_t));
}

/**
 * @brief Ask one name class of an open handle.
 * @param[in] fn_query Entry point of the query.
 * @param[in] handle Handle to ask.
 * @param[in] info_class Class of the query.
 * @param[in] length Size of the buffer of the query, zero asks with the
 *                   default buffer of the probe.
 * @param[out] status Status of the query.
 * @return The name the query reported, empty on failure.
 */
static std::wstring QueryName(T_NtQueryInformationFileProbe fn_query, HANDLE handle, FILE_INFORMATION_CLASS info_class,
                              ULONG length, long& status)
{
    std::vector<BYTE> buffer(length == 0 ? kNameBufferSize : length);
    IO_STATUS_BLOCK   iosb = {};

    const NTSTATUS st = fn_query(handle, &iosb, buffer.data(), static_cast<ULONG>(buffer.size()), info_class);
    status = static_cast<long>(st);
    if (!NT_SUCCESS(st))
    {
        return L"";
    }

    return NameOfAnswer(info_class, buffer, static_cast<ULONG>(buffer.size()));
}

static nlohmann::json ProbeQueryInformationFile_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolQueryInformationFile::Req>();

    appbox::test::ProtocolQueryInformationFile::Rsp rsp;

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn_query =
        ntdll == nullptr
            ? nullptr
            : reinterpret_cast<T_NtQueryInformationFileProbe>(GetProcAddress(ntdll, "NtQueryInformationFile"));

    for (const auto& item : req.items)
    {
        appbox::test::ProtocolQueryInformationFile::Item result;

        if (fn_query == nullptr)
        {
            result.status = static_cast<long>(STATUS_PROCEDURE_NOT_FOUND);
            rsp.items.push_back(result);
            continue;
        }

        const std::wstring path = appbox::UTF8ToWide(item.path);
        const DWORD        access = item.mode == "write" ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;

        HANDLE handle = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            result.status = static_cast<long>(STATUS_OBJECT_NAME_NOT_FOUND);
            rsp.items.push_back(result);
            continue;
        }

        result.name =
            appbox::WideToUTF8(QueryName(fn_query, handle, FileNameInformation, item.length, result.nameStatus));
        result.normalized = appbox::WideToUTF8(
            QueryName(fn_query, handle, FileNormalizedNameInformation, item.length, result.normalizedStatus));
        result.all = appbox::WideToUTF8(QueryName(fn_query, handle, FileAllInformation, item.length, result.allStatus));

        CloseHandle(handle);
        rsp.items.push_back(result);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeQueryInformationFile("QueryInformationFile", ProbeQueryInformationFile_Entry);
