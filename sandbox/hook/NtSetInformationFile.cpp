#include "utils/WinAPI.h" /* Must be first include file */
#include "filesystem/CreateDirectory.hpp"
#include "filesystem/DirName.hpp"
#include "filesystem/FileInformationClass.hpp"
#include "filesystem/IsolationPolicy.hpp"
#include "filesystem/MarkerName.hpp"
#include "filesystem/RemoveAll.hpp"
#include "filesystem/Resolve.hpp"
#include "filesystem/ViewPathOfHandle.hpp"
#include "hook/NtDeleteFile.hpp"
#include "utils/ConvertToFullNtPath.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "NtSetInformationFile.hpp"
#include <cstddef>
#include <cwctype>
#include <vector>

T_NtSetInformationFile sys_NtSetInformationFile = nullptr;

static nlohmann::json NtSetInformationFileLogParam(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                   PVOID FileInformation, ULONG Length,
                                                   FILE_INFORMATION_CLASS FileInformationClass)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = appbox::filesystem::FileInformationClassName(FileInformationClass);
    return param;
}

static appbox::LoggerF logger("NtSetInformationFile", NtSetInformationFileLogParam);

/**
 * @brief Whether a class links an object instead of moving it.
 * @param[in] FileInformationClass Class of the call.
 * @return true for the `FileLinkInformation` family.
 */
static bool SetInformationClassIsLink(FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
    case FileLinkInformation:
    case FileLinkInformationEx:
    case FileLinkInformationBypassAccessCheck:
    case FileLinkInformationExBypassAccessCheck:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Whether a class carries the flags of an extended request.
 *
 * The extended classes replace the `ReplaceIfExists` byte of the plain classes
 * with a set of flags, so the hook has to read and to write the request in the
 * layout of its class.
 *
 * @param[in] FileInformationClass Class of the call.
 * @return true for the `...Ex` and `...ExBypassAccessCheck` classes.
 */
static bool SetInformationClassIsExtended(FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
    case FileRenameInformationEx:
    case FileRenameInformationExBypassAccessCheck:
    case FileLinkInformationEx:
    case FileLinkInformationExBypassAccessCheck:
        return true;
    default:
        return false;
    }
}

/**
 * @brief What a rename or a link asks the view to do.
 */
struct SetNameRequest
{
    /** Whether the call links the object instead of moving it. */
    bool bLink = false;

    /** Whether the call carries the flags of an extended request. */
    bool bExtended = false;

    /** Whether an entry which the view already holds is replaced. */
    bool bReplaceIfExists = false;

    /** Flags of an extended request, copied to the redirected call. */
    ULONG flags = 0;

    /** `ReplaceIfExists` byte of a plain request, copied to the redirected call. */
    BOOLEAN replaceIfExists = FALSE;

    /** Directory the name is relative to, null when the name is a full path. */
    HANDLE rootDirectory = nullptr;

    /** Name the object is moved to or linked at, as the caller spelled it. */
    std::wstring name;
};

/**
 * @brief Offset of the name inside the record of a rename or a link.
 *
 * The plain and the extended layout carry the name at the same offset, because
 * the flags of the extended form replace the `ReplaceIfExists` byte and the
 * padding which follows it.
 */
static const size_t kSetNameOffset = offsetof(FILE_RENAME_INFORMATION, FileName);

/**
 * @brief The extended layout carries the name at the offset of the plain one.
 *
 * The flags of the extended form take the place of the `ReplaceIfExists` byte
 * and of the padding behind it, so a hook which reads and writes a request of
 * either class can use one offset for the name.
 */
static_assert(offsetof(FILE_RENAME_INFORMATION_EX, FileName) == offsetof(FILE_RENAME_INFORMATION, FileName),
              "the plain and the extended request must carry the name at the same offset");
static_assert(offsetof(FILE_LINK_INFORMATION, FileName) == offsetof(FILE_RENAME_INFORMATION, FileName),
              "a link must carry the name at the same offset as a rename");
static_assert(offsetof(FILE_LINK_INFORMATION_EX, FileName) == offsetof(FILE_RENAME_INFORMATION, FileName),
              "an extended link must carry the name at the same offset as a rename");

/**
 * @brief Read the request of a rename or a link out of the buffer of the caller.
 * @param[in] FileInformationClass Class of the call.
 * @param[in] FileInformation Buffer of the caller, may be null.
 * @param[in] Length Size of the buffer of the caller.
 * @param[out] request The request the buffer carries.
 * @return false when the buffer does not carry a complete name, in which case
 *         the call belongs to the file system.
 */
static bool ReadSetNameRequest(FILE_INFORMATION_CLASS FileInformationClass, PVOID FileInformation, ULONG Length,
                               SetNameRequest& request)
{
    if (FileInformation == nullptr || Length < kSetNameOffset)
    {
        return false;
    }

    const auto* plain = reinterpret_cast<const FILE_RENAME_INFORMATION*>(FileInformation);
    request.bLink = SetInformationClassIsLink(FileInformationClass);
    request.bExtended = SetInformationClassIsExtended(FileInformationClass);
    request.rootDirectory = plain->RootDirectory;
    request.flags = request.bExtended ? reinterpret_cast<const FILE_RENAME_INFORMATION_EX*>(FileInformation)->Flags : 0;
    request.replaceIfExists = plain->ReplaceIfExists;
    request.bReplaceIfExists = request.bExtended
                                   ? (request.flags & (request.bLink ? FILE_LINK_FLAG_REPLACE_IF_EXISTS
                                                                     : FILE_RENAME_FLAG_REPLACE_IF_EXISTS)) != 0
                                   : plain->ReplaceIfExists != FALSE;

    const ULONG name_bytes = plain->FileNameLength;
    if (name_bytes == 0 || (name_bytes % sizeof(WCHAR)) != 0 || kSetNameOffset + name_bytes > Length)
    {
        return false;
    }

    request.name.assign(plain->FileName, name_bytes / sizeof(WCHAR));
    return true;
}

/**
 * @brief Build the buffer of a rename or a link which names a layer path.
 *
 * The name of a layer is longer than the name of the view, so the redirected
 * call carries a buffer of its own instead of the one of the caller. The
 * layout of the request is the one of the class the caller used, which is what
 * `SetNameRequest::bExtended` records.
 *
 * @param[in] request Request of the caller, its flags are kept.
 * @param[in] targetPath Path of the layer the object is moved to or linked at.
 * @return Buffer of the redirected call.
 */
static std::vector<BYTE> BuildSetNameRequest(const SetNameRequest& request, const std::wstring& targetPath)
{
    const ULONG       name_bytes = static_cast<ULONG>(targetPath.size() * sizeof(WCHAR));
    std::vector<BYTE> buffer(kSetNameOffset + name_bytes);

    if (request.bExtended)
    {
        auto* ex = reinterpret_cast<PFILE_RENAME_INFORMATION_EX>(buffer.data());
        ex->Flags = request.flags;
        ex->RootDirectory = nullptr;
        ex->FileNameLength = name_bytes;
    }
    else
    {
        auto* plain = reinterpret_cast<PFILE_RENAME_INFORMATION>(buffer.data());
        plain->ReplaceIfExists = request.replaceIfExists;
        plain->RootDirectory = nullptr;
        plain->FileNameLength = name_bytes;
    }

    memcpy(buffer.data() + kSetNameOffset, targetPath.c_str(), name_bytes);
    return buffer;
}

/**
 * @brief Case insensitive comparison of two paths of the view.
 * @param[in] left Path to compare.
 * @param[in] right Path to compare against.
 * @return true when the two paths name the same entry.
 */
static bool EqualsI(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i)
    {
        if (std::towupper(left[i]) != std::towupper(right[i]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Join a name to the directory it is relative to.
 * @param[in] parent Path of the directory.
 * @param[in] child Name below the directory.
 * @return The joined path.
 */
static std::wstring JoinPath(const std::wstring& parent, const std::wstring& child)
{
    std::wstring result = parent;
    while (!result.empty() && result.back() == L'\\')
    {
        result.pop_back();
    }
    result += L'\\';

    size_t offset = 0;
    while (offset < child.size() && child[offset] == L'\\')
    {
        ++offset;
    }

    result += child.substr(offset);
    return result;
}

/**
 * @brief Build the path of the view a rename or a link names.
 *
 * @param[in] request Request of the caller.
 * @param[in] sourceViewPath Path of the view the handle denotes.
 * @param[out] viewPath Path of the view the object is moved to.
 * @return false when the name cannot be expressed as a path of the view, in
 *         which case the call belongs to another isolation domain.
 */
static bool DestinationViewPath(const SetNameRequest& request, const std::wstring& sourceViewPath,
                                std::wstring& viewPath)
{
    if (request.name.empty())
    {
        return false;
    }

    if (request.rootDirectory != nullptr)
    {
        std::wstring rootViewPath;
        if (appbox::filesystem::ViewPathOfHandle(request.rootDirectory, rootViewPath) !=
            appbox::filesystem::HandlePathStatus::View)
        {
            return false;
        }

        viewPath = JoinPath(rootViewPath, request.name);
        return true;
    }

    if (request.name[0] == L'\\')
    {
        /* A name which starts at the root of the object namespace is a path of
         * the view, in the DOS style form the file system expects. */
        return appbox::MappingAsDosNtPath(request.name, viewPath);
    }

    /*
     * A name which is neither a full path nor relative to a directory the
     * caller named is relative to the directory of the object, which is the
     * base the file system resolves it against.
     */
    viewPath = JoinPath(appbox::filesystem::DirName(sourceViewPath), request.name);
    return true;
}

/**
 * @brief Redirect a rename or a link through the view.
 *
 * The name of the call is a path of the view, so the call reaches the real
 * filesystem of the machine when it is forwarded unchanged. The helper
 * resolves the name in the view, decides the layer the new entry lands in with
 * the rule of the isolation, and forwards the call against the path of that
 * layer. A rename moves the object away from the path it had, so the layers
 * which still hold the old name are hidden afterwards.
 *
 * @param[in] FileHandle Handle of the object which is moved or linked.
 * @param[in,out] IoStatusBlock Status block of the call.
 * @param[in] FileInformationClass Class of the call.
 * @param[in] request Request of the caller.
 * @param[out] status Status of the redirected call.
 * @return true when the call was redirected and \p status is its answer, false
 *         when the hook has to forward the call of the caller unchanged.
 */
static bool RedirectSetName(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                            FILE_INFORMATION_CLASS FileInformationClass, const SetNameRequest& request,
                            NTSTATUS& status)
{
    std::wstring sourceViewPath;
    if (appbox::filesystem::ViewPathOfHandle(FileHandle, sourceViewPath) != appbox::filesystem::HandlePathStatus::View)
    {
        LOG_D("the view path of the handle is unknown");
        return false;
    }

    std::wstring destinationViewPath;
    if (!DestinationViewPath(request, sourceViewPath, destinationViewPath))
    {
        LOG_D("the destination is not a path of the view");
        return false;
    }

    /*
     * The names of the markers are reserved: a path which carries one names
     * the view rather than an entry it holds, see `MarkerName.hpp`. A rename
     * and a link report the name they refuse, so neither of them can create a
     * marker.
     */
    const auto marker_placement = appbox::filesystem::ReservedMarkerNamePlacement(destinationViewPath);
    if (marker_placement == appbox::filesystem::MarkerNamePlacement::Parent)
    {
        status = STATUS_OBJECT_PATH_NOT_FOUND;
        return true;
    }
    if (marker_placement == appbox::filesystem::MarkerNamePlacement::Entry)
    {
        status = STATUS_OBJECT_NAME_INVALID;
        return true;
    }

    /*
     * A rename of an object onto the name it already has does not move it, and
     * the file system reports it as a success as well. The object keeps its
     * layer, so nothing has to be hidden and the call never reaches the file
     * system, which would act on the name of the view.
     */
    if (!request.bLink && EqualsI(sourceViewPath, destinationViewPath))
    {
        status = STATUS_SUCCESS;
        return true;
    }

    /*
     * The layer the new entry lands in is decided from every layer which holds
     * the destination, so the resolver has to report all of them: `Merge`
     * applies the modification to the host filesystem when the host holds the
     * entry or when no layer holds it at all.
     */
    appbox::filesystem::ResolveOption resolve_option;
    resolve_option.bStopOnFirstFound = false;

    auto destination = appbox::filesystem::Resolve(destinationViewPath, resolve_option);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*destination)));

    if (!destination->bParentExist)
    {
        status = STATUS_OBJECT_PATH_NOT_FOUND;
        return true;
    }

    /* An entry which the view holds is only replaced when the caller asks for
     * it; an entry the isolation hides does not exist in the view. */
    if (destination->status == appbox::filesystem::ResolveResult::Status::Exists && !request.bReplaceIfExists)
    {
        status = STATUS_OBJECT_NAME_COLLISION;
        return true;
    }

    const bool target_host =
        appbox::filesystem::WritesToHost(destination->isolation, destination->bHostHolds, destination->bSandboxHolds);
    const std::wstring& target_path = target_host ? destination->hostPath : destination->uPath;
    const size_t        target_base = target_host ? destination->hostPathBaseSize : destination->uPathBaseSize;
    LOG_T(L"set name: target={}", target_host ? L"host" : L"view");

    /*
     * A whiteout marker below the destination is the record of a delete of an
     * earlier run: the entry which is created here replaces it, so the marker
     * has to go.
     */
    if (destination->bWhiteoutInUpper)
    {
        appbox::filesystem::RemoveAll(destination->whiteoutPath, OBJ_CASE_INSENSITIVE);
    }

    /* The folders above the new entry have to exist in the layer it lands in. */
    appbox::filesystem::CreateDirectories(appbox::filesystem::DirName(target_path), target_base);

    std::vector<BYTE> buffer = BuildSetNameRequest(request, target_path);
    status = sys_NtSetInformationFile(FileHandle, IoStatusBlock, buffer.data(), static_cast<ULONG>(buffer.size()),
                                      FileInformationClass);
    if (!NT_SUCCESS(status))
    {
        return true;
    }

    /*
     * A rename moved the object away from the path of the view it had, so the
     * layers which still hold that name have to be hidden. A link leaves the
     * object where it is.
     */
    if (!request.bLink)
    {
        const NTSTATUS hide = appbox::HideViewPath(sourceViewPath, OBJ_CASE_INSENSITIVE);
        if (!NT_SUCCESS(hide))
        {
            LOG_W("failed to hide the source of the rename: {}", hide);
        }
    }

    return true;
}

static NTSTATUS Hook_NtSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                          ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    logger.Log(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    if (!appbox::filesystem::SetInformationCarriesPath(FileInformationClass))
    {
        /* The class acts on the handle, which already denotes the layer the
         * view selected. */
        return sys_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    }

    SetNameRequest request;
    if (!ReadSetNameRequest(FileInformationClass, FileInformation, Length, request))
    {
        /* A request which carries no complete name is answered by the file
         * system, which reports the failure the caller expects. */
        return sys_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    }

    NTSTATUS status = STATUS_SUCCESS;
    if (RedirectSetName(FileHandle, IoStatusBlock, FileInformationClass, request, status))
    {
        return status;
    }

    return sys_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

static void LoadNtSetInformationFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtSetInformationFile");
    sys_NtSetInformationFile = reinterpret_cast<T_NtSetInformationFile>(addr);
}

appbox::HookRecord appbox::HookNtSetInformationFile = {
    "NtSetInformationFile",
    LoadNtSetInformationFile,
    (void**)&sys_NtSetInformationFile,
    Hook_NtSetInformationFile,
};
