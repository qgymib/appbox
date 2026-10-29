#include "WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtFsControlFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "ResolveCore.hpp"
#include "ResolveOps.hpp"
#include "Sandbox.hpp"
#include "ReparsePoint.hpp"
#include <climits>
#include <cstddef>
#include <cstring>
#include <vector>

namespace
{

/**
 * @brief Rebuild the data of a reparse point with a translated target.
 * @param[in] data Data of the reparse point as it was read.
 * @param[in] substitute Name the file system follows.
 * @param[in] print Name the shell shows.
 * @param[out] translated The rebuilt data.
 * @return `STATUS_SUCCESS` or a failure when the data cannot be built.
 */
NTSTATUS BuildReparseData(const std::vector<BYTE>& data, const std::wstring& substitute, const std::wstring& print,
                          std::vector<BYTE>& translated)
{
    const auto*       buffer = reinterpret_cast<const REPARSE_DATA_BUFFER*>(data.data());
    const bool        symlink = buffer->ReparseTag == IO_REPARSE_TAG_SYMLINK;
    const std::size_t pathBufferOffset =
        symlink ? appbox::filesystem::kSymbolicLinkPathBufferOffset : appbox::filesystem::kMountPointPathBufferOffset;
    const ULONG flags = symlink ? buffer->ReparseBuffer.SymbolicLinkReparseBuffer.Flags : 0;

    const std::size_t substituteBytes = (substitute.size() + 1) * sizeof(WCHAR);
    const std::size_t printBytes = (print.size() + 1) * sizeof(WCHAR);
    const std::size_t nameBytes = substituteBytes + printBytes;

    if (substitute.size() * sizeof(WCHAR) > USHRT_MAX || print.size() * sizeof(WCHAR) > USHRT_MAX ||
        pathBufferOffset + nameBytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    const std::size_t dataLength = (pathBufferOffset - appbox::filesystem::kReparseHeaderSize) + nameBytes;

    translated.assign(pathBufferOffset + nameBytes, 0);
    auto* out = reinterpret_cast<REPARSE_DATA_BUFFER*>(translated.data());
    out->ReparseTag = buffer->ReparseTag;
    out->ReparseDataLength = static_cast<USHORT>(dataLength);
    out->Reserved = 0;

    WCHAR* pathBuffer = nullptr;
    if (symlink)
    {
        auto& variant = out->ReparseBuffer.SymbolicLinkReparseBuffer;
        variant.SubstituteNameOffset = 0;
        variant.SubstituteNameLength = static_cast<USHORT>(substitute.size() * sizeof(WCHAR));
        variant.PrintNameOffset = static_cast<USHORT>(substituteBytes);
        variant.PrintNameLength = static_cast<USHORT>(print.size() * sizeof(WCHAR));
        variant.Flags = flags;
        pathBuffer = variant.PathBuffer;
    }
    else
    {
        auto& variant = out->ReparseBuffer.MountPointReparseBuffer;
        variant.SubstituteNameOffset = 0;
        variant.SubstituteNameLength = static_cast<USHORT>(substitute.size() * sizeof(WCHAR));
        variant.PrintNameOffset = static_cast<USHORT>(substituteBytes);
        variant.PrintNameLength = static_cast<USHORT>(print.size() * sizeof(WCHAR));
        pathBuffer = variant.PathBuffer;
    }

    memcpy(pathBuffer, substitute.c_str(), substituteBytes);
    memcpy(reinterpret_cast<BYTE*>(pathBuffer) + substituteBytes, print.c_str(), printBytes);
    return STATUS_SUCCESS;
}

/**
 * @brief Open an object of a layer without following it.
 *
 * The call uses the original entry point of the process, which keeps the
 * resolution of the view from running again, and it asks for the reparse point
 * itself, so the file system reports the link instead of the object it names.
 *
 * @param[in] layerPath Path of the object inside its layer.
 * @param[in] desiredAccess Access the caller asks for.
 * @param[out] handle Handle of the object.
 * @return `STATUS_SUCCESS` or the failure of the file system.
 */
NTSTATUS OpenLayerObject(const std::wstring& layerPath, ACCESS_MASK desiredAccess, HANDLE& handle)
{
    handle = nullptr;

    UNICODE_STRING name;
    sys_RtlInitUnicodeString(&name, layerPath.c_str());

    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    IO_STATUS_BLOCK iosb;
    return sys_NtCreateFile(&handle, desiredAccess | SYNCHRONIZE, &attributes, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                            FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
}

/**
 * @brief Create the object of the overlay which carries a reparse point.
 * @param[in] layerPath Path the object takes in the overlay.
 * @param[in] directory Whether the object is a folder.
 * @return `STATUS_SUCCESS` or the failure of the file system.
 */
NTSTATUS CreateLayerEntry(const std::wstring& layerPath, bool directory)
{
    UNICODE_STRING name;
    sys_RtlInitUnicodeString(&name, layerPath.c_str());

    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE          handle = nullptr;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status = sys_NtCreateFile(
        &handle, FILE_GENERIC_WRITE | FILE_LIST_DIRECTORY, &attributes, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, directory ? FILE_OPEN_IF : FILE_OVERWRITE_IF,
        (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE) | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);

    if (NT_SUCCESS(status))
    {
        sys_NtClose(handle);
    }
    return status;
}

/**
 * @brief Write the data of a reparse point into an object of the overlay.
 * @param[in] layerPath Path of the object.
 * @param[in] data Data to write.
 * @return `STATUS_SUCCESS` or the failure of the file system.
 */
NTSTATUS WriteReparseData(const std::wstring& layerPath, const std::vector<BYTE>& data)
{
    HANDLE   handle = nullptr;
    NTSTATUS status = OpenLayerObject(layerPath, GENERIC_WRITE, handle);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    IO_STATUS_BLOCK iosb;
    status = sys_NtFsControlFile(handle, nullptr, nullptr, nullptr, &iosb, FSCTL_SET_REPARSE_POINT,
                                 const_cast<BYTE*>(data.data()), static_cast<ULONG>(data.size()), nullptr, 0);
    sys_NtClose(handle);
    return status;
}

} // namespace

NTSTATUS appbox::filesystem::ReadReparseData(const std::wstring& layerPath, std::vector<BYTE>& data)
{
    data.clear();
    if (layerPath.empty())
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * The object is opened with the access the control code needs and nothing
     * more: a legacy junction of the profile carries a security descriptor
     * which refuses every other access, and the file system reads the data of
     * a reparse point for a handle which may read the attributes of the object.
     */
    HANDLE   handle = nullptr;
    NTSTATUS status = OpenLayerObject(layerPath, FILE_READ_ATTRIBUTES, handle);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    data.resize(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);

    IO_STATUS_BLOCK iosb;
    status = sys_NtFsControlFile(handle, nullptr, nullptr, nullptr, &iosb, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                                 data.data(), static_cast<ULONG>(data.size()));
    sys_NtClose(handle);

    /*
     * The file system reports a buffer which is too small with
     * `STATUS_BUFFER_OVERFLOW`, which `NT_SUCCESS()` refuses even though the
     * data it copied is complete: the length of the record is reported as well.
     */
    if (status == STATUS_BUFFER_OVERFLOW && iosb.Information > 0 && iosb.Information <= data.size())
    {
        status = STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(status))
    {
        data.clear();
        return status;
    }

    data.resize(iosb.Information);
    return STATUS_SUCCESS;
}

NTSTATUS appbox::filesystem::ViewNamespaceTarget(const std::wstring& target, std::wstring& viewTarget)
{
    if (appbox::sandbox == nullptr)
    {
        return STATUS_UNSUCCESSFUL;
    }

    return ViewNamespaceTargetCore(target, appbox::sandbox->fs, viewTarget);
}

NTSTATUS appbox::filesystem::ReparseTargetToViewPath(const std::vector<BYTE>& data, const std::wstring& linkViewPath,
                                                     std::wstring& targetViewPath)
{
    if (appbox::sandbox == nullptr)
    {
        return STATUS_UNSUCCESSFUL;
    }

    return ReparseTargetToViewPathCore(data, linkViewPath, appbox::sandbox->fs, targetViewPath);
}

NTSTATUS appbox::filesystem::TranslateReparseDataToView(const std::vector<BYTE>& data, std::vector<BYTE>& translated)
{
    ReparseTarget target;
    if (!ReadReparseTarget(data, target))
    {
        /* A tag the view does not resolve keeps the data of the caller. */
        translated = data;
        return STATUS_SUCCESS;
    }

    if (target.relative)
    {
        /*
         * A relative target names the same entry in the view as it does in the
         * layer, so it is stored as the caller spelled it.
         */
        translated = data;
        return STATUS_SUCCESS;
    }

    std::wstring   substitute;
    const NTSTATUS status = ViewNamespaceTarget(target.substitute, substitute);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    std::wstring print = target.print;
    if (!print.empty())
    {
        std::wstring viewPrint;
        if (NT_SUCCESS(ViewNamespaceTarget(print, viewPrint)))
        {
            print = viewPrint;
        }
    }

    return BuildReparseData(data, substitute, print, translated);
}

NTSTATUS appbox::filesystem::CopyReparsePointEntry(const std::wstring& sourceLayerPath,
                                                   const std::wstring& destinationLayerPath, ULONG sourceAttributes)
{
    std::vector<BYTE> data;
    NTSTATUS          status = ReadReparseData(sourceLayerPath, data);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    const bool directory = (sourceAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    /* The object has to exist before its reparse point can be written. */
    status = CreateLayerEntry(destinationLayerPath, directory);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return WriteReparseData(destinationLayerPath, data);
}

NTSTATUS appbox::filesystem::ExpandReparsePoints(const ResolveFs& fs, const std::wstring& viewPath,
                                                 ReparseFollowMode mode, ULONG nameAttributes,
                                                 const IsolationTable* isolation, std::wstring& effectiveViewPath)
{
    return ExpandReparsePointsCore(fs, viewPath, mode, nameAttributes, isolation, SandboxResolveOps(),
                                   effectiveViewPath);
}
