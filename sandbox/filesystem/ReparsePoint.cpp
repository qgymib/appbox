#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "filesystem/DirName.hpp"
#include "filesystem/LayerPath.hpp"
#include "filesystem/StreamName.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtFsControlFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "ReparsePoint.hpp"
#include <climits>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <vector>

namespace
{

/**
 * @brief Size of the header of the data of a reparse point.
 *
 * The header carries the tag, the length of the data behind it and a reserved
 * field. The two variants the view understands follow it with the four fields
 * which count the two names of the target, and the symbolic link adds the flags
 * which mark a relative target behind them.
 */
constexpr std::size_t kReparseHeaderSize = sizeof(ULONG) + 2 * sizeof(USHORT);

/** Offset of the name buffer of a mount point. */
constexpr std::size_t kMountPointPathBufferOffset = kReparseHeaderSize + 4 * sizeof(USHORT);

/** Offset of the name buffer of a symbolic link, which carries its flags first. */
constexpr std::size_t kSymbolicLinkPathBufferOffset = kMountPointPathBufferOffset + sizeof(ULONG);

/**
 * @brief The target of a reparse point the view resolves.
 */
struct ReparseTarget
{
    /** Name the file system follows. */
    std::wstring substitute;

    /** Name the shell shows, may be empty. */
    std::wstring print;

    /** Whether the name is relative to the directory of the link. */
    bool relative = false;
};

/**
 * @brief Whether a path starts with a prefix, ignoring the case.
 * @param[in] path Path to inspect.
 * @param[in] prefix Prefix to compare against.
 * @return true when the path starts with the prefix.
 */
bool StartsWithI(const std::wstring& path, const std::wstring& prefix)
{
    if (path.size() < prefix.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index)
    {
        if (std::towlower(path[index]) != std::towlower(prefix[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Tag of the data of a reparse point.
 * @param[in] data Data of a reparse point.
 * @return The tag, zero when the data is too short to carry one.
 */
ULONG ReparseTagOf(const std::vector<BYTE>& data)
{
    if (data.size() < sizeof(ULONG))
    {
        return 0;
    }

    ULONG tag = 0;
    memcpy(&tag, data.data(), sizeof(tag));
    return tag;
}

/**
 * @brief Read one of the counted names of the data of a reparse point.
 *
 * The name is addressed by an offset and a length relative to the name buffer
 * of the variant, both counted in bytes. The data comes from the file system of
 * a layer, so every length is validated before it is read: a record which is
 * shorter than the name it announces is refused instead of read past its end.
 *
 * @param[in] data Data of the reparse point.
 * @param[in] pathBufferOffset Offset of the name buffer inside the data.
 * @param[in] offset Offset of the name inside the name buffer.
 * @param[in] length Length of the name in bytes.
 * @param[out] name The name.
 * @return true when the name is inside the data.
 */
bool ReadReparseName(const std::vector<BYTE>& data, std::size_t pathBufferOffset, USHORT offset, USHORT length,
                     std::wstring& name)
{
    if ((length % sizeof(WCHAR)) != 0)
    {
        return false;
    }

    const std::size_t begin = pathBufferOffset + offset;
    const std::size_t end = begin + length;
    if (end < begin || end > data.size())
    {
        return false;
    }

    name.assign(reinterpret_cast<const wchar_t*>(data.data() + begin), length / sizeof(WCHAR));
    return true;
}

/**
 * @brief Read the target of the data of a reparse point.
 * @param[in] data Data of the reparse point.
 * @param[out] target The target the data carries.
 * @return true when the data carries a target the view understands.
 */
bool ReadReparseTarget(const std::vector<BYTE>& data, ReparseTarget& target)
{
    /* The four fields which count the names have to be inside the data. */
    if (data.size() < kReparseHeaderSize + 4 * sizeof(USHORT))
    {
        return false;
    }

    const auto* buffer = reinterpret_cast<const REPARSE_DATA_BUFFER*>(data.data());

    USHORT      substituteOffset = 0;
    USHORT      substituteLength = 0;
    USHORT      printOffset = 0;
    USHORT      printLength = 0;
    std::size_t pathBufferOffset = 0;

    if (buffer->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
    {
        const auto& variant = buffer->ReparseBuffer.MountPointReparseBuffer;
        substituteOffset = variant.SubstituteNameOffset;
        substituteLength = variant.SubstituteNameLength;
        printOffset = variant.PrintNameOffset;
        printLength = variant.PrintNameLength;
        pathBufferOffset = kMountPointPathBufferOffset;
    }
    else if (buffer->ReparseTag == IO_REPARSE_TAG_SYMLINK)
    {
        if (data.size() < kSymbolicLinkPathBufferOffset)
        {
            return false;
        }

        const auto& variant = buffer->ReparseBuffer.SymbolicLinkReparseBuffer;
        substituteOffset = variant.SubstituteNameOffset;
        substituteLength = variant.SubstituteNameLength;
        printOffset = variant.PrintNameOffset;
        printLength = variant.PrintNameLength;
        pathBufferOffset = kSymbolicLinkPathBufferOffset;
        target.relative = (variant.Flags & SYMLINK_FLAG_RELATIVE) != 0;
    }
    else
    {
        return false;
    }

    return ReadReparseName(data, pathBufferOffset, substituteOffset, substituteLength, target.substitute) &&
           ReadReparseName(data, pathBufferOffset, printOffset, printLength, target.print);
}

/**
 * @brief Split a path of the view into its root and its components.
 *
 * The root is the namespace of the object manager together with the drive, for
 * example `\??\C:`. Only a path of that shape is a path of the view: a path
 * which names no drive belongs to another isolation domain.
 *
 * @param[in] path Path of the view.
 * @param[out] root The root of the path.
 * @param[out] components The components below the root.
 * @return true when the path carries a drive.
 */
bool SplitViewPath(const std::wstring& path, std::wstring& root, std::vector<std::wstring>& components)
{
    std::size_t begin = 0;
    if (StartsWithI(path, L"\\??\\"))
    {
        begin = 4;
    }
    else if (StartsWithI(path, L"\\GLOBAL??\\"))
    {
        begin = 10;
    }
    else
    {
        return false;
    }

    if (path.size() < begin + 2 || path[begin + 1] != L':')
    {
        return false;
    }

    const wchar_t letter = path[begin];
    if (!((letter >= L'A' && letter <= L'Z') || (letter >= L'a' && letter <= L'z')))
    {
        return false;
    }

    root = path.substr(0, begin + 2);
    components.clear();

    std::size_t pos = begin + 2;
    while (pos < path.size())
    {
        while (pos < path.size() && path[pos] == L'\\')
        {
            ++pos;
        }
        if (pos >= path.size())
        {
            break;
        }

        const std::size_t separator = path.find(L'\\', pos);
        const std::size_t end = separator == std::wstring::npos ? path.size() : separator;
        components.push_back(path.substr(pos, end - pos));
        pos = end;
    }

    return true;
}

/**
 * @brief Build the path of a prefix of the components.
 * @param[in] root Root of the path.
 * @param[in] components Components of the path.
 * @param[in] count Number of components to append.
 * @return The path of the prefix.
 */
std::wstring JoinComponents(const std::wstring& root, const std::vector<std::wstring>& components, std::size_t count)
{
    std::wstring result = root;
    for (std::size_t index = 0; index < count; ++index)
    {
        result += L'\\';
        result += components[index];
    }
    return result;
}

/**
 * @brief Append the components which follow a reparse point to its target.
 * @param[in] target Path the reparse point names.
 * @param[in] components Components of the path which carries the link.
 * @param[in] from Index of the first component to append.
 * @return The path of the view which the caller reaches.
 */
std::wstring JoinRemainder(const std::wstring& target, const std::vector<std::wstring>& components, std::size_t from)
{
    if (from >= components.size())
    {
        return target;
    }

    std::wstring result = target;
    while (!result.empty() && result.back() == L'\\')
    {
        result.pop_back();
    }

    for (std::size_t index = from; index < components.size(); ++index)
    {
        result += L'\\';
        result += components[index];
    }
    return result;
}

/**
 * @brief Remove the components which name the current or the parent directory.
 * @param[in] path Path of the view.
 * @param[out] normalized Path without a `.` or a `..` component.
 * @return `STATUS_SUCCESS`, or a failure when the path leaves its drive.
 */
NTSTATUS NormalizeViewPath(const std::wstring& path, std::wstring& normalized)
{
    std::wstring              root;
    std::vector<std::wstring> components;
    if (!SplitViewPath(path, root, components))
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    std::vector<std::wstring> collapsed;
    collapsed.reserve(components.size());
    for (const auto& component : components)
    {
        if (component.empty() || component == L".")
        {
            continue;
        }

        if (component == L"..")
        {
            /* The target leaves the drive of the view, which no layer holds. */
            if (collapsed.empty())
            {
                return STATUS_REPARSE_POINT_NOT_RESOLVED;
            }
            collapsed.pop_back();
            continue;
        }

        collapsed.push_back(component);
    }

    if (collapsed.empty())
    {
        normalized = root + L"\\";
        return STATUS_SUCCESS;
    }

    normalized = JoinComponents(root, collapsed, collapsed.size());
    return STATUS_SUCCESS;
}

/**
 * @brief Resolve one reparse point of a path of the view.
 *
 * The components are walked from the root, so a component is looked up without
 * an unexpanded link above it: the prefix which is inspected is the entry of
 * the query, which the file system does not follow. The first component which
 * is a reparse point of a redirecting tag ends the walk and its target is
 * appended with the components behind it.
 *
 * @param[in] fs Resolve file system.
 * @param[in] viewPath Path of the view to expand.
 * @param[in] followFinal Whether the entry the path names may be expanded.
 * @param[in] nameAttributes Lookup attributes of the call.
 * @param[in] isolation Isolation modes of the virtual filesystem, may be null.
 * @param[out] expanded Path of the view which the caller reaches, the path
 *                      itself when it carries no redirecting reparse point.
 * @return `STATUS_SUCCESS` or the failure of the view.
 */
NTSTATUS ExpandFirstReparsePoint(const appbox::filesystem::ResolveFs& fs, const std::wstring& viewPath,
                                 bool followFinal, ULONG nameAttributes,
                                 const appbox::filesystem::IsolationTable* isolation, std::wstring& expanded)
{
    std::wstring              root;
    std::vector<std::wstring> components;
    if (!SplitViewPath(viewPath, root, components))
    {
        expanded = viewPath;
        return STATUS_SUCCESS;
    }

    for (std::size_t index = 0; index < components.size(); ++index)
    {
        if (index + 1 == components.size() && !followFinal)
        {
            break;
        }

        const std::wstring prefix = JoinComponents(root, components, index + 1);

        appbox::filesystem::ResolveOption option;
        option.bStopOnFirstFound = true;
        option.NameAttributes = nameAttributes;
        option.reparseFollow = appbox::filesystem::ReparseFollowMode::None;

        auto probe = appbox::filesystem::ResolveFull(fs, prefix, option, isolation);
        if (probe->status != appbox::filesystem::ResolveResult::Status::Exists || probe->hPath.empty())
        {
            /* No visible layer holds the component, so nothing hangs below it. */
            break;
        }

        const auto& hit = probe->hPath[0];
        if ((hit.fInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        {
            continue;
        }

        std::vector<BYTE> data;
        const NTSTATUS    read = appbox::filesystem::ReadReparseData(hit.fPath, data);
        if (read == STATUS_NOT_A_REPARSE_POINT)
        {
            /* The entry carries the attribute without the data of a link. */
            continue;
        }
        if (!NT_SUCCESS(read))
        {
            LOG_W(L"failed to read the reparse data of {}: {}", hit.fPath, read);
            return STATUS_REPARSE_POINT_NOT_RESOLVED;
        }
        if (!appbox::filesystem::IsRedirectingReparseTag(ReparseTagOf(data)))
        {
            /* The tag describes the entry itself, the layer keeps following it. */
            continue;
        }

        std::wstring   target;
        const NTSTATUS translated = appbox::filesystem::ReparseTargetToViewPath(data, prefix, target);
        if (!NT_SUCCESS(translated))
        {
            LOG_W(L"the reparse point of {} names no path of the view", prefix);
            return translated;
        }

        LOG_D(L"reparse point: {} -> {}", prefix, target);
        expanded = JoinRemainder(target, components, index + 1);
        return STATUS_SUCCESS;
    }

    expanded = viewPath;
    return STATUS_SUCCESS;
}

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
    const std::size_t pathBufferOffset = symlink ? kSymbolicLinkPathBufferOffset : kMountPointPathBufferOffset;
    const ULONG       flags = symlink ? buffer->ReparseBuffer.SymbolicLinkReparseBuffer.Flags : 0;

    const std::size_t substituteBytes = (substitute.size() + 1) * sizeof(WCHAR);
    const std::size_t printBytes = (print.size() + 1) * sizeof(WCHAR);
    const std::size_t nameBytes = substituteBytes + printBytes;

    if (substitute.size() * sizeof(WCHAR) > USHRT_MAX || print.size() * sizeof(WCHAR) > USHRT_MAX ||
        pathBufferOffset + nameBytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    const std::size_t dataLength = (pathBufferOffset - kReparseHeaderSize) + nameBytes;

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

bool appbox::filesystem::IsRedirectingReparseTag(ULONG tag)
{
    return tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_SYMLINK;
}

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
    if (target.empty())
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    /*
     * A caller may hold the path of a layer: the file system reports that path
     * for a handle, so a link which an application creates out of a handle it
     * holds would otherwise store the layout of the sandbox.
     */
    std::wstring viewPath;
    if (RebaseLayerPathToView(target, viewPath))
    {
        viewTarget = viewPath;
        return STATUS_SUCCESS;
    }

    /*
     * A path of the view is a DOS style path, and a device path which names a
     * local volume is one as well: the mapping turns `\Device\HarddiskVolumeX`
     * and `\??\Volume{GUID}` into the drive of the view, and refuses the
     * namespaces which belong to another isolation domain.
     */
    if (MappingAsDosNtPath(target, viewPath))
    {
        viewTarget = viewPath;
        return STATUS_SUCCESS;
    }

    return STATUS_REPARSE_POINT_NOT_RESOLVED;
}

NTSTATUS appbox::filesystem::ReparseTargetToViewPath(const std::vector<BYTE>& data, const std::wstring& linkViewPath,
                                                     std::wstring& targetViewPath)
{
    ReparseTarget target;
    if (!ReadReparseTarget(data, target))
    {
        return STATUS_NOT_A_REPARSE_POINT;
    }

    if (target.substitute.empty())
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    if (target.relative)
    {
        /*
         * The view maps a layer by replacing the prefix of the path, so the
         * components below the link keep their shape and a target which is
         * relative to the directory of the link names the same entry in the
         * view as it does in the layer.
         */
        std::wstring joined = DirName(linkViewPath);
        if (joined.empty())
        {
            return STATUS_REPARSE_POINT_NOT_RESOLVED;
        }
        if (joined.back() != L'\\')
        {
            joined += L'\\';
        }
        joined += target.substitute;

        return NormalizeViewPath(joined, targetViewPath);
    }

    std::wstring   viewPath;
    const NTSTATUS status = ViewNamespaceTarget(target.substitute, viewPath);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return NormalizeViewPath(viewPath, targetViewPath);
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
    if (mode == ReparseFollowMode::None)
    {
        effectiveViewPath = viewPath;
        return STATUS_SUCCESS;
    }

    /*
     * A stream belongs to the file which carries it, so the entry the expansion
     * follows is the file and the name of the stream is appended to the path
     * the file resolves to.
     */
    const std::wstring streamName = StreamNameOf(viewPath);
    std::wstring       current = EntryPathOfStream(viewPath);

    for (std::size_t depth = 0; depth <= kMaxReparseDepth; ++depth)
    {
        std::wstring   expanded;
        const NTSTATUS status =
            ExpandFirstReparsePoint(fs, current, mode == ReparseFollowMode::All, nameAttributes, isolation, expanded);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        if (expanded == current)
        {
            effectiveViewPath = streamName.empty() ? current : current + L":" + streamName;
            return STATUS_SUCCESS;
        }

        current = std::move(expanded);
    }

    LOG_W(L"the chain of reparse points of {} is too deep", viewPath);
    return STATUS_REPARSE_POINT_NOT_RESOLVED;
}
