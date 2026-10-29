#include "WinAPI.h" /* Must be first include file */
#include <spdlog/spdlog.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "ResolveCore.hpp"
#include "FilesystemIsolationTable.hpp"
#include "KnownFolder.hpp"
#include "utils/ConvertDosPathToNtPath.hpp"
#include "WString.hpp"
#include "ViewPathResolver.hpp"

namespace
{

/**
 * @brief Remove the namespace which opens the drive of a DOS style NT path.
 *
 * The resolver answers the paths of the layers in the form of the file system,
 * which carries the namespace of the object manager, while the system calls of
 * the launcher expect a DOS path.
 *
 * @param[in] nt_path Path of the form `\??\C:\dir\file`.
 * @return The path of the form `C:\dir\file`, the input when it carries no
 *         namespace the launcher knows.
 */
std::wstring ToDosPath(const std::wstring& nt_path)
{
    constexpr wchar_t kGlobalNamespace[] = L"\\GLOBAL??\\";
    constexpr wchar_t kNamespace[] = L"\\??\\";

    if (nt_path.compare(0, std::size(kGlobalNamespace) - 1, kGlobalNamespace) == 0)
    {
        return nt_path.substr(std::size(kGlobalNamespace) - 1);
    }

    if (nt_path.compare(0, std::size(kNamespace) - 1, kNamespace) == 0)
    {
        return nt_path.substr(std::size(kNamespace) - 1);
    }

    return nt_path;
}

/**
 * @brief Look up the attributes of one path of a layer or of the host.
 *
 * The resolver treats every failure as a missing entry, so the call only has to
 * tell the two apart: an entry which does not exist and a call which fails for
 * another reason both report a status which is not a success.
 *
 * @param[in] path Path to look up.
 * @param[in] attributes Attributes of the lookup, unused by the system call.
 * @param[out] info Attributes of the entry, may be null.
 * @return `STATUS_SUCCESS` or the failure of the lookup.
 */
NTSTATUS QueryAttributes(const std::wstring& path, ULONG attributes, PFILE_BASIC_INFORMATION info)
{
    (void)attributes;

    const auto dos_path = ToDosPath(path);

    WIN32_FILE_ATTRIBUTE_DATA data;
    ZeroMemory(&data, sizeof(data));
    if (!GetFileAttributesExW(dos_path.c_str(), GetFileExInfoStandard, &data))
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME)
        {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        return STATUS_UNSUCCESSFUL;
    }

    if (info != nullptr)
    {
        ZeroMemory(info, sizeof(*info));
        info->FileAttributes = data.dwFileAttributes;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Read the data of a reparse point of one path.
 *
 * The object is opened with `FILE_FLAG_OPEN_REPARSE_POINT`, so the file system
 * reports the link itself instead of the object it names, and the data is read
 * with `FSCTL_GET_REPARSE_POINT`, exactly like the sandbox reads it.
 *
 * @param[in] layer_path Path of the object inside its layer.
 * @param[out] data Data of the reparse point.
 * @return `STATUS_SUCCESS`, `STATUS_NOT_A_REPARSE_POINT` when the object
 *         carries no data of a reparse point, or the failure of the system.
 */
NTSTATUS ReadReparseData(const std::wstring& layer_path, std::vector<BYTE>& data)
{
    data.clear();
    if (layer_path.empty())
    {
        return STATUS_INVALID_PARAMETER;
    }

    const auto dos_path = ToDosPath(layer_path);

    HANDLE handle =
        CreateFileW(dos_path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return GetLastError() == ERROR_NOT_A_REPARSE_POINT ? STATUS_NOT_A_REPARSE_POINT : STATUS_UNSUCCESSFUL;
    }

    data.resize(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);

    DWORD       returned = 0;
    const BOOL  read = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, data.data(),
                                       static_cast<DWORD>(data.size()), &returned, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(handle);

    if (!read)
    {
        data.clear();
        return error == ERROR_NOT_A_REPARSE_POINT ? STATUS_NOT_A_REPARSE_POINT : STATUS_UNSUCCESSFUL;
    }

    data.resize(returned);
    return STATUS_SUCCESS;
}

/**
 * @brief Report a diagnostic of the resolution into the log of the run.
 * @param[in] message Message to report.
 */
void LogWarn(const std::wstring& message)
{
    SPDLOG_WARN("{}", appbox::WideToUTF8(message));
}

/**
 * @brief Report a trace of the resolution into the log of the run.
 * @param[in] message Message to report.
 */
void LogDebug(const std::wstring& message)
{
    SPDLOG_DEBUG("{}", appbox::WideToUTF8(message));
}

/**
 * @brief Get the operations the launcher hands to the shared resolver.
 *
 * The launcher runs outside of the sandbox, so the calls of the resolver reach
 * the file system directly instead of the entry points a sandboxed process
 * saved before it attached its hooks.
 *
 * @return The operations of the launcher.
 */
const appbox::filesystem::ResolveOps& LauncherResolveOps()
{
    static const appbox::filesystem::ResolveOps ops = {
        QueryAttributes,
        ReadReparseData,
        LogWarn,
        LogDebug,
    };

    return ops;
}

/**
 * @brief Describe why a path of the view has no image.
 * @param[in] status Status of the resolution.
 * @return The reason, as a text of the log and of the error message.
 */
const char* StatusText(appbox::filesystem::ResolveResult::Status status)
{
    switch (status)
    {
    case appbox::filesystem::ResolveResult::Status::Exists:
        return "the entry exists";
    case appbox::filesystem::ResolveResult::Status::NotFound:
        return "no layer of the view and no path of the host holds it";
    case appbox::filesystem::ResolveResult::Status::HiddenByWhiteout:
        return "a whiteout marker of the overlay hides it";
    case appbox::filesystem::ResolveResult::Status::BlockedByOpaque:
        return "an opaque marker of the overlay hides it";
    case appbox::filesystem::ResolveResult::Status::HiddenByIsolation:
        return "the isolation mode of the view hides it";
    }

    return "the view cannot resolve it";
}

} // namespace

appbox::ViewPathResolver::ViewPathResolver(const appbox::SandboxConfig& config)
{
    fs_.fs_upper = appbox::UTF8ToWide(config.fs_upper);

    /*
     * The isolation file of a layer lists paths whose first component is the
     * layer key of a preset directory, so the table needs the key of every
     * layer next to the folder the layer is mapped to. The keys are derived
     * from the host paths of the layers, exactly like the sandbox derives them.
     */
    std::vector<appbox::filesystem::IsolationLayer> layers;

    for (const auto& lower : config.fs_lower)
    {
        appbox::filesystem::ResolveFsMapping mapping;
        mapping.mapped_nt_path = appbox::UTF8ToWide(lower.mapped_nt_path);
        mapping.host_nt_path = appbox::UTF8ToWide(lower.host_nt_path);
        fs_.fs_lower.push_back(mapping);

        const auto layer_key = appbox::filesystem::IsolationTable::LayerKeyOf(mapping.host_nt_path);
        if (!layer_key.empty())
        {
            layers.push_back(appbox::filesystem::IsolationLayer{ layer_key, mapping.mapped_nt_path });
        }
    }

    /*
     * The files are read in the order of the layers of the run: the file of
     * the resources of the archive first, then the file of every patch package
     * in ascending order. Every file overrides the modes the files below it set
     * for the same path, which is the rule the sandbox applies as well.
     */
    for (const auto& path : config.filesystem_isolation_dos_paths)
    {
        std::ifstream stream(std::filesystem::path(appbox::UTF8ToWide(path)), std::ios::binary);
        if (!stream.is_open())
        {
            SPDLOG_DEBUG("the filesystem isolation file does not exist: {}", path);
            continue;
        }

        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        std::vector<std::wstring> unmapped;
        std::string               error;
        if (!isolation_.Parse(text, layers, unmapped, error))
        {
            SPDLOG_WARN("the filesystem isolation file '{}' is ignored: {}", path, error);
            continue;
        }

        for (const auto& entry : unmapped)
        {
            SPDLOG_WARN("the filesystem isolation file '{}' lists a path which no layer of the view maps: {}", path,
                        appbox::WideToUTF8(entry));
        }
    }

    SPDLOG_DEBUG("the view of the run holds {} layer(s) and {} isolation mode(s)", fs_.fs_lower.size(),
                 isolation_.Count());
}

bool appbox::ViewPathResolver::Resolve(const std::wstring& view_path, std::wstring& host_path, std::string& error) const
{
    host_path.clear();
    error.clear();

    /*
     * A path of the configuration may name a preset directory of the packer
     * instead of a folder of this machine, so the layer key is expanded first.
     * A path without a key is a path of the view already and stays unchanged.
     */
    std::wstring dos_path;
    try
    {
        dos_path = appbox::ExpandKnownFolder(view_path);
    }
    catch (const std::exception& e)
    {
        error = "the known folder of the image cannot be resolved: " + appbox::WideToUTF8(view_path) + " (" + e.what() +
                ")";
        return false;
    }

    if (dos_path.empty())
    {
        error = "the path of the image is empty";
        return false;
    }

    std::wstring nt_path;
    const DWORD  converted = appbox::ConvertDosPathToNtPath(dos_path, nt_path);
    if (converted != 0)
    {
        error = "the path of the image cannot be resolved: " + appbox::WideToUTF8(dos_path) + " (" +
                std::to_string(converted) + ")";
        return false;
    }

    appbox::filesystem::ResolveOption option;
    option.bStopOnFirstFound = true;
    option.reparseFollow = appbox::filesystem::ReparseFollowMode::All;

    const auto result = appbox::filesystem::ResolveCore(fs_, nt_path, option, &isolation_, LauncherResolveOps());

    /*
     * A reparse point which the view cannot expand fails the resolution: the
     * layer must not follow a link the view never decided about, which is the
     * rule the sandbox applies to every path of an application as well.
     */
    if (!NT_SUCCESS(result->reparseStatus))
    {
        error = "a reparse point of the path names no path of the view: " + appbox::WideToUTF8(dos_path);
        return false;
    }

    if (result->status != appbox::filesystem::ResolveResult::Status::Exists || result->hPath.empty())
    {
        error = "the image was not found in the sandbox view: " + appbox::WideToUTF8(dos_path) + " (" +
                StatusText(result->status) + ")";
        return false;
    }

    host_path = ToDosPath(result->hPath[0].fPath);
    if (host_path.empty())
    {
        error = "the layer of the image has no host path: " + appbox::WideToUTF8(dos_path);
        return false;
    }

    SPDLOG_DEBUG("the image of '{}' is '{}'", appbox::WideToUTF8(dos_path), appbox::WideToUTF8(host_path));
    return true;
}
