#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <cstddef>
#include <string>
#include <vector>
#include "WString.hpp"
#include "SetInformationFile.hpp"

/* clang-format off */
typedef NTSTATUS (*T_NtSetInformationFileProbe)(
    /* [IN] */  HANDLE                  FileHandle,
    /* [OUT] */ PIO_STATUS_BLOCK        IoStatusBlock,
    /* [IN] */  PVOID                   FileInformation,
    /* [IN] */  ULONG                   Length,
    /* [IN] */  FILE_INFORMATION_CLASS  FileInformationClass
);
/* clang-format on */

/** Access a rename needs on the handle of the object. */
static const ACCESS_MASK kDeleteAccess = DELETE | SYNCHRONIZE;

/** Access a link is content with, which does not copy the object up. */
static const ACCESS_MASK kReadAccess = GENERIC_READ | SYNCHRONIZE;

/** The probe shares the object like a caller which only moves it. */
static const ULONG kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

/**
 * @brief Convert a Win32 path to the name the entry point takes.
 *
 * The name of a rename and of a link is a path of the object manager, so a
 * drive letter has to be opened by the namespace of the object manager. A name
 * which is not a Win32 path is a name relative to the directory of the object
 * and is kept as the caller spelled it.
 *
 * @param[in] path Path or name of the request.
 * @return The name the entry point expects.
 */
static std::wstring AsNtName(const std::wstring& path)
{
    if (path.size() >= 2 && path[1] == L':')
    {
        std::wstring nt_path = path;
        nt_path.insert(0, L"\\??\\");
        return nt_path;
    }

    return path;
}

/**
 * @brief Map the action of an item onto the class of the call.
 * @param[in] action Action of the item.
 * @param[out] FileInformationClass Class of the call.
 * @param[out] bExtended Whether the action uses the extended layout.
 * @param[out] bLink Whether the action links the object.
 * @return false when the action is unknown.
 */
static bool ActionToClass(const std::string& action, FILE_INFORMATION_CLASS& FileInformationClass, bool& bExtended,
                          bool& bLink)
{
    if (action == "rename")
    {
        FileInformationClass = FileRenameInformation;
        bExtended = false;
        bLink = false;
        return true;
    }
    if (action == "rename_ex")
    {
        FileInformationClass = FileRenameInformationEx;
        bExtended = true;
        bLink = false;
        return true;
    }
    if (action == "link")
    {
        FileInformationClass = FileLinkInformation;
        bExtended = false;
        bLink = true;
        return true;
    }
    if (action == "link_ex")
    {
        FileInformationClass = FileLinkInformationEx;
        bExtended = true;
        bLink = true;
        return true;
    }
    return false;
}

static nlohmann::json ProbeSetInformationFile_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolSetInformationFile::Req>();

    appbox::test::ProtocolSetInformationFile::Rsp rsp;

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn_set = ntdll == nullptr
                      ? nullptr
                      : reinterpret_cast<T_NtSetInformationFileProbe>(GetProcAddress(ntdll, "NtSetInformationFile"));

    for (const auto& item : req.items)
    {
        appbox::test::ProtocolSetInformationFile::Item result;

        FILE_INFORMATION_CLASS info_class = FileRenameInformation;
        bool                   extended = false;
        bool                   link = false;
        if (fn_set == nullptr || !ActionToClass(item.action, info_class, extended, link))
        {
            result.status = static_cast<long>(STATUS_PROCEDURE_NOT_FOUND);
            rsp.items.push_back(result);
            continue;
        }

        const std::wstring source = appbox::UTF8ToWide(item.source);
        const ACCESS_MASK  access = item.access == "read" ? kReadAccess : kDeleteAccess;
        HANDLE             handle =
            CreateFileW(source.c_str(), access, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            result.status = static_cast<long>(STATUS_OBJECT_NAME_NOT_FOUND);
            rsp.items.push_back(result);
            continue;
        }

        HANDLE root = nullptr;
        if (!item.rootDirectory.empty())
        {
            const std::wstring root_path = appbox::UTF8ToWide(item.rootDirectory);
            root = CreateFileW(root_path.c_str(), FILE_LIST_DIRECTORY | SYNCHRONIZE, kShareAll, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (root == INVALID_HANDLE_VALUE)
            {
                root = nullptr;
            }
        }

        const std::wstring target = AsNtName(appbox::UTF8ToWide(item.target));
        const size_t       name_offset = offsetof(FILE_RENAME_INFORMATION, FileName);
        const ULONG        name_bytes = static_cast<ULONG>(target.size() * sizeof(wchar_t));

        std::vector<BYTE> buffer(name_offset + name_bytes);
        if (extended)
        {
            auto* info = reinterpret_cast<PFILE_RENAME_INFORMATION_EX>(buffer.data());
            info->Flags = item.replaceIfExists
                              ? (link ? FILE_LINK_FLAG_REPLACE_IF_EXISTS : FILE_RENAME_FLAG_REPLACE_IF_EXISTS)
                              : 0;
            info->RootDirectory = root;
            info->FileNameLength = name_bytes;
        }
        else
        {
            auto* info = reinterpret_cast<PFILE_RENAME_INFORMATION>(buffer.data());
            info->ReplaceIfExists = item.replaceIfExists ? TRUE : FALSE;
            info->RootDirectory = root;
            info->FileNameLength = name_bytes;
        }
        memcpy(buffer.data() + name_offset, target.c_str(), name_bytes);

        IO_STATUS_BLOCK iosb = {};
        result.status =
            static_cast<long>(fn_set(handle, &iosb, buffer.data(), static_cast<ULONG>(buffer.size()), info_class));

        CloseHandle(handle);
        if (root != nullptr)
        {
            CloseHandle(root);
        }

        rsp.items.push_back(result);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeSetInformationFile("SetInformationFile", ProbeSetInformationFile_Entry);
