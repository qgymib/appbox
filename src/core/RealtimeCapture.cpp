#include "RealtimeCapture.hpp"

/*
 * The kernel headers have to see a version which carries the manifest
 * providers and the system logger mode, and the definition has to come before
 * the first windows.h of the translation unit.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "EtwEventDecoder.hpp"
#include "PresetDirectory.hpp"
#include "SnapshotCapture.hpp"
#include "WString.hpp"
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief The manifest provider of the file events of the kernel.
 *
 * The recorder subscribes to the manifest providers of the kernel rather than
 * to its classic groups: the events of a manifest carry their properties by
 * name, so the recorder reads them with the trace data helper of Windows and
 * never with an offset into a payload whose layout the version of the machine
 * decides.
 */
constexpr GUID kKernelFileGuid = {
    0xEDD08927, 0x9CC4, 0x4E65, { 0xB9, 0x70, 0xC2, 0x56, 0x0F, 0xB5, 0xC2, 0x89 }
};

/**
 * @brief The classic provider of the registry events of the kernel.
 *
 * The registry group of the kernel is enabled as well and the registry is read
 * through the events of the classic provider, because the events of the
 * manifest provider cannot name the key of a value event: the kernel leaves the
 * `KeyName` of its value and key events empty, its create and open events name
 * a key relative to the base object they carry, and it has no event which
 * enumerates the keys which existed before the session started. The classic
 * provider names the key of a value event through its key control block, and
 * the rundown of its group reports the blocks of the keys which exist, so a
 * change of a key which was opened before the recording began is resolved as
 * well.
 */
constexpr GUID kRegistryGuid = {
    0xAE53722E, 0xC863, 0x11D2, { 0x86, 0x59, 0x00, 0xC0, 0x4F, 0xA3, 0x21, 0xA1 }
};

/**
 * @brief Name of the session of the capture.
 *
 * The manifest providers of the kernel can only be enabled on a system logger
 * session which a program opens for itself, and the legacy kernel logger of
 * the machine refuses them. The session of the capture is therefore a session
 * of its own, which also means that a capture no longer takes the shared
 * kernel logger of the machine away from another tool.
 */
constexpr const wchar_t* kSessionName = L"AppBoxRealtimeCapture";

/**
 * @brief The keywords the file provider is asked for when its manifest is
 *        unknown.
 *
 * The capture asks the machine for the keywords of the events it reads, which
 * its manifest declares, see FileProviderEnableFrom(). The table below is the
 * one the version of Windows the capture was written against declares, and it
 * is the fallback of a machine whose manifest cannot be read: the values are
 * not part of the headers of the Windows SDK.
 */
constexpr std::uint64_t kKernelFileKeywords = 0x10 | 0x20 | 0x80 | 0x200 | 0x400 | 0x800 | 0x1000;

/**
 * @brief The size of one buffer of the session, in kilobytes.
 *
 * The buffers of the session are what carries the events a provider wrote
 * until the consumer drains them, and the session drops the events of a burst
 * which fills them while nothing consumes the session. The registry rundown
 * the kernel reports when the registry group is enabled is such a burst: it
 * names the block of every key the kernel holds, so the buffers are sized to
 * carry it and the events which follow it.
 */
constexpr unsigned long kSessionBufferSize = 256;

/**
 * @brief Number of buffers the session starts with.
 *
 * A real time session asks for at least two buffers per processor, so the
 * value is the one of a machine with several processors.
 */
constexpr unsigned long kSessionMinimumBuffers = 32;

/**
 * @brief Number of buffers the session may grow to.
 */
constexpr unsigned long kSessionMaximumBuffers = 64;

/**
 * @brief The size of the log file of the session, in megabytes.
 *
 * The session of the capture is a real time session, so the file is never
 * written; the size is what the system logger of the machine asks for.
 */
constexpr unsigned long kSessionMaximumFileSize = 100;

/**
 * @brief How long the start of a recording waits for its consumer, in
 *        milliseconds.
 *
 * The consumer opens the trace of the session on a thread of its own, so the
 * start waits for it before a provider is enabled. A consumer which does not
 * report itself in that time cannot be waited for any longer, because the
 * session holds the kernel providers of the machine.
 */
constexpr unsigned kConsumerAttachMilliseconds = 10000;

/** The event identifiers of the manifest file provider of the kernel. */
constexpr unsigned short kFileEventName = 10;
constexpr unsigned short kFileEventNameRundown = 11;
constexpr unsigned short kFileEventCreate = 12;
constexpr unsigned short kFileEventWrite = 16;
constexpr unsigned short kFileEventDeletePath = 26;
constexpr unsigned short kFileEventRenamePath = 27;
constexpr unsigned short kFileEventSetLinkPath = 28;
constexpr unsigned short kFileEventCreateNewFile = 30;

/** The event types of the classic registry provider of the kernel. */
constexpr unsigned short kRegOpDeleteKey = 12;
constexpr unsigned short kRegOpSetValue = 14;
constexpr unsigned short kRegOpDeleteValue = 15;
constexpr unsigned short kRegOpKcbCreate = 22;
constexpr unsigned short kRegOpKcbDelete = 23;
constexpr unsigned short kRegOpKcbRundown = 25;

/**
 * @brief The markers which keep the warnings of the capture apart.
 *
 * The identifier of an event is only unique per provider, so a warning about
 * an event which could not be resolved names the provider as well.
 */
constexpr unsigned kFileProvider = 1;
constexpr unsigned kRegistryProvider = 2;
constexpr unsigned kWppProvider = 3;

/**
 * @brief Size of the session property buffer.
 *
 * The buffer carries the session name and the file name strings behind the
 * structure, and it has to stay alive until the session was stopped.
 */
constexpr std::size_t kPropertiesSize = sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(wchar_t);

/**
 * @brief Number of file objects the path map keeps.
 *
 * A long recording walks through more objects than the map needs to hold, so
 * the map is dropped when it grows past the limit and fills again from the
 * creates which follow. The writes of a file whose object was dropped cannot
 * be resolved, which the recording counts as an unresolved event.
 */
constexpr std::size_t kMaxFileObjects = 100000;

/**
 * @brief Number of events between two progress reports.
 */
constexpr std::size_t kProgressEvents = 256;

/**
 * @brief Time between two progress reports.
 */
constexpr std::chrono::milliseconds kProgressInterval{ 200 };

/**
 * @brief Whether two GUIDs are the same.
 * @param[in] left Left GUID.
 * @param[in] right Right GUID.
 * @return true when both GUIDs are equal.
 */
bool SameGuid(const GUID& left, const GUID& right)
{
    return memcmp(&left, &right, sizeof(GUID)) == 0;
}

/**
 * @brief Whether two texts are equal ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts differ in the case of their characters alone.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    return ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

/**
 * @brief Whether a path is at or below another one.
 *
 * The ancestor has to be a whole component prefix, so `C:\Program FilesX` is
 * not below `C:\Program Files`.
 *
 * @param[in] path Path to test.
 * @param[in] ancestor Folder to test against.
 * @return true when the path is the ancestor itself or below it.
 */
bool IsPathAtOrBelow(const std::wstring& path, const std::wstring& ancestor)
{
    if (ancestor.empty() || path.size() < ancestor.size())
    {
        return false;
    }
    if (!EqualsIgnoreCase(path.substr(0, ancestor.size()), ancestor))
    {
        return false;
    }

    return path.size() == ancestor.size() || path[ancestor.size()] == L'\\';
}

/**
 * @brief Resolve the volume device a root of the host names.
 *
 * The final path of the root is what the file system resolves into the volume
 * device the kernel reports the files below, which answers the question for a
 * drive letter and for the root of a volume which is mounted at a folder.
 * `QueryDosDeviceW` answers it for a drive letter as well and is the fallback
 * of a build on which the final path of a root cannot be read.
 *
 * @param[in] root Path of the root of a volume, e.g. `L"C:\\"` or the name of a
 *            volume, e.g. `L"\\\\?\\Volume{...}\\"`.
 * @param[in] drive Drive letter with its colon, empty for a volume which has
 *            none, e.g. `L"C:"`.
 * @param[out] device The volume device the root names, e.g.
 *                `L"\\Device\\HarddiskVolume3"`.
 * @return true when the root was resolved.
 */
bool ResolveVolumeDevice(const std::wstring& root, const wchar_t* drive, std::wstring& device)
{
    const HANDLE handle = ::CreateFileW(root.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
    {
        wchar_t    buffer[512] = {};
        const auto length = ::GetFinalPathNameByHandleW(handle, buffer, 512, FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
        ::CloseHandle(handle);
        if (length != 0 && length < 512)
        {
            device.assign(buffer, length);
            while (!device.empty() && device.back() == L'\\')
            {
                device.pop_back();
            }
            return !device.empty();
        }
    }

    if (drive != nullptr && drive[0] != L'\0')
    {
        wchar_t buffer[512] = {};
        if (::QueryDosDeviceW(drive, buffer, sizeof(buffer) / sizeof(buffer[0])) > 0)
        {
            device = buffer;
            return !device.empty();
        }
    }

    return false;
}

/**
 * @brief Add the volumes which are mounted at a folder of another volume.
 *
 * A volume of the machine is reached through a drive letter or through a folder
 * of another volume. The drives are what the table holds by default, so the
 * volumes which have no drive letter are added here: the mount point of such a
 * volume is the path a reader of the capture recognizes and the device of the
 * volume is the one the kernel reports.
 *
 * A volume which has a drive letter is left alone, because the table already
 * holds it and two entries of one device would decide the translation twice.
 *
 * @param[in,out] map The table which is extended.
 */
void AddMountedVolumes(std::vector<std::pair<std::wstring, std::wstring>>& map)
{
    wchar_t    name[512] = {};
    const auto found = ::FindFirstVolumeW(name, sizeof(name) / sizeof(name[0]));
    if (found == INVALID_HANDLE_VALUE)
    {
        return;
    }

    std::wstring volume = name;
    HANDLE       search = found;
    for (;;)
    {
        wchar_t      points[1024] = {};
        DWORD        length = sizeof(points) / sizeof(points[0]);
        std::wstring folder;
        bool         has_drive = false;

        if (::GetVolumePathNamesForVolumeNameW(volume.c_str(), points, length, &length) != FALSE)
        {
            for (const wchar_t* point = points; *point != L'\0';)
            {
                const std::wstring mount_point(point);
                point += mount_point.size() + 1;

                if (mount_point.size() >= 3 && mount_point[1] == L':')
                {
                    has_drive = true;
                    break;
                }

                if (folder.empty())
                {
                    folder = mount_point;
                }
            }
        }

        std::wstring device;
        if (!has_drive && !folder.empty() && ResolveVolumeDevice(volume, L"", device))
        {
            while (folder.size() > 3 && folder.back() == L'\\')
            {
                folder.pop_back();
            }
            map.emplace_back(std::move(device), std::move(folder));
        }

        wchar_t next[512] = {};
        if (::FindNextVolumeW(search, next, sizeof(next) / sizeof(next[0])) == FALSE)
        {
            break;
        }
        volume = next;
    }

    ::FindVolumeClose(search);
}

/**
 * @brief The volumes of the host as a translation table.
 *
 * The kernel reports the files below the device names of the volumes, so the
 * analysis needs the table which exchanges them for the paths of the host: the
 * drive letters of the machine and the mount points of the volumes which have
 * none.
 *
 * @return Volume prefixes and the paths they are reached by, longest prefix
 *         first.
 */
std::vector<std::pair<std::wstring, std::wstring>> BuildDeviceMap()
{
    std::vector<std::pair<std::wstring, std::wstring>> map;

    wchar_t    drives[512] = {};
    const auto count = ::GetLogicalDriveStringsW(sizeof(drives) / sizeof(drives[0]), drives);
    if (count != 0 && count < sizeof(drives) / sizeof(drives[0]))
    {
        for (unsigned long offset = 0; offset < count;)
        {
            const std::wstring drive(drives + offset);
            offset += static_cast<unsigned long>(drive.size()) + 1;

            std::wstring device;
            if (!ResolveVolumeDevice(drive, drive.substr(0, 2).c_str(), device))
            {
                continue;
            }

            map.emplace_back(std::move(device), drive.substr(0, 2)); /* `E:` without the trailing slash. */
        }
    }

    AddMountedVolumes(map);

    std::sort(map.begin(), map.end(),
              [](const auto& left, const auto& right) { return left.first.size() > right.first.size(); });
    return map;
}

/**
 * @brief The SID of the current user as text.
 * @return The SID, empty when the token cannot be read.
 */
std::wstring CurrentUserSid()
{
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        return {};
    }

    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0)
    {
        ::CloseHandle(token);
        return {};
    }

    std::vector<unsigned char> buffer(size);
    std::wstring               sid;
    if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE)
    {
        wchar_t* text = nullptr;
        if (::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text) != FALSE)
        {
            sid = text;
            ::LocalFree(text);
        }
    }

    ::CloseHandle(token);
    return sid;
}

/**
 * @brief The leaf name of a process image.
 *
 * The lookup opens the process and asks for its image path; a process which
 * is gone already yields the empty name, so the event keeps no process name
 * instead.
 *
 * @param[in] process_id Identifier of the process.
 * @return The leaf name of the image, empty when it cannot be read.
 */
std::wstring ProcessImageName(DWORD process_id)
{
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr)
    {
        return {};
    }

    wchar_t      path[MAX_PATH] = {};
    DWORD        size = MAX_PATH;
    std::wstring name;
    if (::QueryFullProcessImageNameW(process, 0, path, &size) != FALSE)
    {
        name = std::filesystem::path(path).filename().wstring();
    }

    ::CloseHandle(process);
    return name;
}

/**
 * @brief Read one value of the host registry.
 *
 * The path is the path of the registry view, and the root name of it is
 * exchanged for the predefined handle of the process before the key is
 * opened. A value which cannot be read yields false, which the caller
 * reports as an entry the analysis had to leave out.
 *
 * @param[in] path View path of the key which holds the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] type The `REG_*` type code of the value.
 * @param[out] data The raw data of the value.
 * @return true when the value was read.
 */
bool ReadRegistryValue(const std::wstring& path, const std::wstring& name, std::uint32_t& type,
                       std::vector<std::uint8_t>& data)
{
    struct RootKey
    {
        const wchar_t* name;
        HKEY           handle;
    };

    static const RootKey kRoots[] = {
        { L"HKEY_CLASSES_ROOT",   HKEY_CLASSES_ROOT   },
        { L"HKEY_CURRENT_USER",   HKEY_CURRENT_USER   },
        { L"HKEY_LOCAL_MACHINE",  HKEY_LOCAL_MACHINE  },
        { L"HKEY_USERS",          HKEY_USERS          },
        { L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
    };

    const auto separator = path.find(L'\\');
    const auto root_name = separator == std::wstring::npos ? path : path.substr(0, separator);
    const auto sub_key = separator == std::wstring::npos ? std::wstring() : path.substr(separator + 1);

    HKEY root = nullptr;
    for (const auto& candidate : kRoots)
    {
        if (EqualsIgnoreCase(root_name, candidate.name))
        {
            root = candidate.handle;
            break;
        }
    }
    if (root == nullptr)
    {
        return false;
    }

    HKEY key = root;
    if (!sub_key.empty() && ::RegOpenKeyExW(root, sub_key.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type_code = 0;
    DWORD size = 0;
    bool  read = ::RegQueryValueExW(key, name.c_str(), nullptr, &type_code, nullptr, &size) == ERROR_SUCCESS;
    if (read)
    {
        data.resize(size);
        read = size == 0 ||
               ::RegQueryValueExW(key, name.c_str(), nullptr, &type_code, data.data(), &size) == ERROR_SUCCESS;
        if (read)
        {
            data.resize(size);
            type = type_code;
        }
    }

    if (key != root)
    {
        ::RegCloseKey(key);
    }
    return read;
}

/**
 * @brief The state one path of the filesystem carries through the analysis.
 */
enum class FileState
{
    Added,   ///< The entry was created or overwritten during the recording.
    Touched, ///< An existing entry was written through an open handle.
    Removed, ///< The entry is gone, and it existed before the recording.
};

/**
 * @brief One path of the filesystem together with its state.
 */
struct FileEntryState
{
    std::wstring path; ///< Win32 path of the entry.
    FileState    state = FileState::Touched;
};

/**
 * @brief The state one registry value carries through the analysis.
 */
enum class ValueState
{
    Set,     ///< The value was written during the recording.
    Deleted, ///< The value was deleted during the recording.
};

/**
 * @brief One registry value together with its state.
 */
struct ValueEntryState
{
    std::wstring path; ///< View path of the key which holds the value.
    std::wstring name; ///< Name of the value.
    ValueState   state = ValueState::Set;
};

/**
 * @brief Map an absolute host path onto a preset directory.
 *
 * The presets overlap: the folders of the user profile hang below the profile
 * itself, so the preset with the longest real path wins, which keeps a file of
 * the local application data away from the profile of the user.
 *
 * @param[in] path Win32 path of the host.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @param[out] preset_id Identifier of the preset directory which owns the path.
 * @param[out] relative Path below the preset directory.
 * @return true when the path is part of a preset directory.
 */
bool MapToPreset(const std::wstring& path, const std::vector<appbox::PresetDirectory>& presets, std::string& preset_id,
                 std::wstring& relative)
{
    const appbox::PresetDirectory* best = nullptr;
    for (const auto& preset : presets)
    {
        if (!IsPathAtOrBelow(path, preset.real_path))
        {
            continue;
        }
        if (best == nullptr || preset.real_path.size() > best->real_path.size())
        {
            best = &preset;
        }
    }
    if (best == nullptr)
    {
        return false;
    }

    preset_id = best->id;
    relative = path.substr(best->real_path.size());
    while (!relative.empty() && relative.front() == L'\\')
    {
        relative.erase(relative.begin());
    }
    return !relative.empty();
}

/**
 * @brief Build the message of a change which belongs to no preset directory.
 * @param[in] path Path of the change on the host.
 * @return The message.
 */
std::wstring UnmappedMessage(const std::wstring& path)
{
    return L"'" + path + L"' belongs to no preset directory of the view, so the change was not captured";
}

/**
 * @brief Add the folders which hold the state the shell of the machine rewrites.
 *
 * The shell is not noise as a whole: the user creates, renames and removes
 * files with it, and those changes are the ones a capture has to carry. What
 * the shell rewrites without being asked is its own state, and the folders
 * below are where it keeps that state: the items the user opened recently with
 * their jump lists, the libraries of the shell, the thumbnails and the icons it
 * caches, the caches of the content it shows and the database of the
 * notifications it draws.
 *
 * The folders are named below the preset directories of the machine, so a
 * machine whose folders are somewhere else is covered as well.
 *
 * @param[in,out] options The options which receive the folders.
 */
void AddShellStateFolders(appbox::RealtimeCaptureOptions& options)
{
    /**
     * @brief One folder of the state of the shell below a preset directory.
     */
    struct ShellStateFolder
    {
        const char*    preset_id; ///< Preset directory which holds the state.
        const wchar_t* relative;  ///< Path of the state below that directory.
    };

    static const ShellStateFolder kFolders[] = {
        { "application_data",       L"Microsoft\\Windows\\Recent"        },
        { "application_data",       L"Microsoft\\Windows\\Libraries"     },
        { "local_application_data", L"Microsoft\\Windows\\Explorer"      },
        { "local_application_data", L"Microsoft\\Windows\\Caches"        },
        { "local_application_data", L"Microsoft\\Windows\\INetCache"     },
        { "local_application_data", L"Microsoft\\Windows\\WebCache"      },
        { "local_application_data", L"Microsoft\\Windows\\Notifications" },
    };

    for (const auto& folder : kFolders)
    {
        appbox::PresetDirectory preset;
        if (!appbox::FindPresetDirectory(folder.preset_id, preset) || preset.real_path.empty())
        {
            SPDLOG_WARN("failed to resolve the preset directory of the state of the shell: {}", folder.preset_id);
            continue;
        }

        options.excluded_file_paths.push_back(preset.real_path + L"\\" + folder.relative);
    }
}

/**
 * @brief Add the registry keys which hold the state the shell rewrites.
 *
 * A change of the shell is a change of the registry as well: the keys below
 * hold the state the shell keeps for itself, which it rewrites while the user
 * works and which no capture has to carry. The keys are named in full, so the
 * parts of the registry an application describes are left alone.
 *
 * @param[in,out] options The options which receive the keys.
 */
void AddShellStateKeys(appbox::RealtimeCaptureOptions& options)
{
    static const wchar_t* const kKeys[] = {
        L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer",
        L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore",
        L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\UFH",
        L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\Shell",
        L"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\ShellNoRoam",
        L"HKEY_CURRENT_USER\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell",
    };

    for (const auto* key : kKeys)
    {
        options.excluded_registry_paths.push_back(key);
    }
}

} // namespace

namespace appbox
{

RealtimeCaptureOptions DefaultRealtimeCaptureOptions()
{
    RealtimeCaptureOptions options;

    /*
     * The packer itself is noise: it reads the machine for the analysis and
     * writes the workspaces of the session, and its own writes are never part
     * of the change an installation made.
     */
    wchar_t self[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, self, MAX_PATH) > 0)
    {
        options.excluded_processes.push_back(std::filesystem::path(self).filename().wstring());
    }

    /*
     * The services which rewrite the machine on their own, without a user
     * asking for it. An installer runs as its own process, so the noise of
     * the system does not hide the change of an installation. The shell is not
     * one of them: the user works with it, so the changes it makes are changes
     * of the installation, and the state it rewrites on its own is left out by
     * the folders and the keys below, see AddShellStateFolders().
     */
    static const wchar_t* const kProcesses[] = {
        L"System",
        L"Idle",
        L"Registry",
        L"smss.exe",
        L"csrss.exe",
        L"wininit.exe",
        L"winlogon.exe",
        L"services.exe",
        L"lsass.exe",
        L"dwm.exe",
        L"fontdrvhost.exe",
        L"MsMpEng.exe",
        L"NisSrv.exe",
        L"SearchIndexer.exe",
        L"SearchProtocolHost.exe",
        L"SearchFilterHost.exe",
        L"TiWorker.exe",
        L"TrustedInstaller.exe",
        L"svchost.exe",
        L"WmiPrvSE.exe",
        L"wermgr.exe",
        L"Compattelrunner.exe",
        L"conhost.exe",
        L"dllhost.exe",
        L"RuntimeBroker.exe",
        L"sihost.exe",
        L"taskhostw.exe",
    };
    for (const auto* name : kProcesses)
    {
        options.excluded_processes.push_back(name);
    }

    /*
     * The places of the host which carry the transient state of the machine
     * and no application content. The temp folder of the user lives below a
     * preset directory, so the extracts of an installer would travel into the
     * archive without the entry of their own.
     */
    wchar_t temp[MAX_PATH] = {};
    if (::GetTempPathW(MAX_PATH, temp) > 0)
    {
        std::wstring folder(temp);
        while (!folder.empty() && folder.back() == L'\\')
        {
            folder.pop_back();
        }
        if (!folder.empty())
        {
            options.excluded_file_paths.push_back(std::move(folder));
        }
    }

    static const wchar_t* const kFolders[] = {
        L"C:\\Windows\\Temp",
        L"C:\\Windows\\Prefetch",
        L"C:\\Windows\\SoftwareDistribution",
        L"C:\\Windows\\LiveKernelReports",
        L"C:\\$Recycle.Bin",
    };
    for (const auto* folder : kFolders)
    {
        options.excluded_file_paths.push_back(folder);
    }

    /* The state the shell rewrites on its own while a user works. */
    AddShellStateFolders(options);
    AddShellStateKeys(options);

    return options;
}

bool RealtimeCaptureEventExcluded(const RealtimeCaptureEvent& event, const RealtimeCaptureOptions& options)
{
    for (const auto& process : options.excluded_processes)
    {
        if (!process.empty() && EqualsIgnoreCase(event.process, process))
        {
            return true;
        }
    }

    const bool  registry = event.kind == RealtimeCaptureEvent::Kind::ValueSet ||
                           event.kind == RealtimeCaptureEvent::Kind::ValueDeleted ||
                           event.kind == RealtimeCaptureEvent::Kind::KeyDeleted;
    const auto& paths = registry ? options.excluded_registry_paths : options.excluded_file_paths;
    for (const auto& prefix : paths)
    {
        if (IsPathAtOrBelow(event.path, prefix))
        {
            return true;
        }
    }

    return false;
}

std::wstring NtRegistryPathToViewPath(const std::wstring& nt_path, const std::wstring& current_sid)
{
    static const std::wstring kMachinePrefix = L"\\REGISTRY\\MACHINE";
    static const std::wstring kUserPrefix = L"\\REGISTRY\\USER\\";

    /* The private hives of the loader and the transactions are not part of
     * the view the workspace shows. */
    if (nt_path.rfind(L"\\REGISTRY\\A\\", 0) == 0)
    {
        return {};
    }

    if (EqualsIgnoreCase(nt_path.substr(0, kMachinePrefix.size()), kMachinePrefix))
    {
        return L"HKEY_LOCAL_MACHINE" + nt_path.substr(kMachinePrefix.size());
    }

    if (EqualsIgnoreCase(nt_path.substr(0, kUserPrefix.size()), kUserPrefix))
    {
        const auto rest = nt_path.substr(kUserPrefix.size());
        const auto separator = rest.find(L'\\');
        const auto hive = separator == std::wstring::npos ? rest : rest.substr(0, separator);
        const auto below = separator == std::wstring::npos ? std::wstring() : rest.substr(separator);

        if (!current_sid.empty() && EqualsIgnoreCase(hive, current_sid))
        {
            return L"HKEY_CURRENT_USER" + below;
        }

        return L"HKEY_USERS\\" + hive + below;
    }

    return {};
}

namespace
{

/**
 * @brief Canonicalize a path the kernel reported.
 *
 * The kernel reports the paths of the file events with the separators the
 * caller used, so the name of a folder can arrive with a trailing separator
 * and a name which was joined onto a folder can carry a doubled one. The view
 * names every entry with single separators and without a trailing one, and two
 * spellings of the same entry would describe it twice, so the path is
 * canonicalized where it is translated.
 *
 * @param[in] path Path to canonicalize.
 * @return The canonical path, the root of a volume keeps its separator.
 */
std::wstring CanonicalPath(const std::wstring& path)
{
    std::wstring result;
    result.reserve(path.size());

    for (const auto character : path)
    {
        if (character == L'\\' && !result.empty() && result.back() == L'\\')
        {
            continue;
        }

        result.push_back(character);
    }

    while (result.size() > 1 && result.back() == L'\\' && result[result.size() - 2] != L':')
    {
        result.pop_back();
    }

    return result;
}

} // namespace

std::wstring NtFilePathToWin32Path(const std::wstring&                                       nt_path,
                                   const std::vector<std::pair<std::wstring, std::wstring>>& device_map)
{
    for (const auto& device : device_map)
    {
        if (nt_path.size() > device.first.size() && nt_path[device.first.size()] == L'\\' &&
            EqualsIgnoreCase(nt_path.substr(0, device.first.size()), device.first))
        {
            return CanonicalPath(device.second + nt_path.substr(device.first.size()));
        }
    }

    return {};
}

bool RealtimeCaptureSupported()
{
    const auto module = ::GetModuleHandleW(L"ntdll.dll");
    if (module == nullptr)
    {
        return false;
    }

    const auto version =
        reinterpret_cast<LONG(WINAPI*)(PRTL_OSVERSIONINFOW)>(::GetProcAddress(module, "RtlGetVersion"));
    if (version == nullptr)
    {
        return false;
    }

    RTL_OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    if (version(&info) != 0)
    {
        return false;
    }

    /* The kernel events of the capture came with Windows 10. */
    return info.dwMajorVersion >= 10;
}

bool IsProcessElevated()
{
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        return false;
    }

    TOKEN_ELEVATION elevation = {};
    DWORD           size = 0;
    const bool elevated = ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE &&
                          elevation.TokenIsElevated != 0;

    ::CloseHandle(token);
    return elevated;
}

namespace
{

/**
 * @brief The counters the session of a recording reported when it was stopped.
 *
 * The kernel counts the events it dropped while no consumer drained the
 * buffers of the session: an event which was dropped is a change the recording
 * never saw, so the recording reports the counters instead of leaving the loss
 * without a trace. The counters are only available once the session was
 * stopped, because the session is gone from that moment on.
 */
struct SessionStatistics
{
    /** @brief Whether the session reported the counters. */
    bool available = false;

    /** @brief Number of the events the kernel dropped. */
    unsigned long events_lost = 0;

    /** @brief Number of the buffers the kernel dropped in real time mode. */
    unsigned long real_time_buffers_lost = 0;

    /** @brief Number of the buffers the kernel dropped on their way to a file. */
    unsigned long log_buffers_lost = 0;

    /** @brief Number of the buffers the session wrote. */
    unsigned long buffers_written = 0;

    /** @brief Number of the buffers the session held when it was stopped. */
    unsigned long number_of_buffers = 0;

    /** @brief Number of the buffers which were free when the session stopped. */
    unsigned long free_buffers = 0;
};

/**
 * @brief Number of the events of one kind the report of a recording describes.
 *
 * A recording observes the whole machine, so the events it cannot resolve are
 * the rule and not the exception: the report describes the first events of a
 * kind, which is what tells a reader which process made the change and which
 * path the event carried.
 */
constexpr std::size_t kUnresolvedDetails = 16;

/** Number of the properties of an event the report of a recording names. */
constexpr std::size_t kPropertyDetails = 24;

/** Number of the metadata tables the report of a recording names. */
constexpr std::size_t kMetadataDetails = 8;

/**
 * @brief Number of the kinds of the events of another provider the report
 *        names.
 *
 * The recording reads the events of two providers, and the events of another
 * provider are left out: naming the first kinds of them is what tells a reader
 * whether the machine reports a change through a provider the capture does not
 * read, which is the case a capture has to tell apart from a machine which
 * reports no change at all.
 */
constexpr std::size_t kIgnoredDetails = 8;

/** Number of the bytes of a payload the report of a recording names. */
constexpr std::size_t kPayloadDetails = 48;

/** Number of the changes of one kind the report of a recording names. */
constexpr std::size_t kChangeDetails = 2;

/**
 * @brief The reason an event of a recording was left out.
 *
 * The reasons are counted and reported per kind of event, because a recording
 * which leaves an event out has to tell what it left out and why: the count of
 * the events which could not be resolved alone does not say whether the
 * machine wrote an event the capture does not know or whether the path of an
 * event names no volume of the host.
 */
enum class UnresolvedReason
{
    WppMessage,       ///< The event is a message of the software trace preprocessor.
    UnknownProvider,  ///< The machine does not know the provider of the event.
    NoObject,         ///< The event names no object the recording maps.
    NoName,           ///< The event names no path and no name of a value.
    UntranslatedPath, ///< The path of the event names no volume of the host.
    UnknownObject,    ///< The object of the event was never seen created.
    NotInView,        ///< The path of the registry key is not part of the view.
};

/**
 * @brief The name of a reason as the report of a recording spells it.
 * @param[in] reason The reason.
 * @return The name of the reason.
 */
const char* UnresolvedReasonName(UnresolvedReason reason)
{
    switch (reason)
    {
    case UnresolvedReason::WppMessage:
        return "the event is a message of the software trace preprocessor";
    case UnresolvedReason::UnknownProvider:
        return "the machine does not know the provider of the event";
    case UnresolvedReason::NoObject:
        return "the event names no object";
    case UnresolvedReason::NoName:
        return "the event names no path and no name of a value";
    case UnresolvedReason::UntranslatedPath:
        return "the path of the event names no volume of the host";
    case UnresolvedReason::UnknownObject:
        return "the object of the event was never seen created";
    case UnresolvedReason::NotInView:
        return "the path of the registry key is not part of the view";
    }

    return "the reason is unknown";
}

/**
 * @brief The name of a kind of change as the report of a recording spells it.
 * @param[in] kind The kind of the change.
 * @return The name of the kind.
 */
const char* ChangeKindName(RealtimeCaptureEvent::Kind kind)
{
    switch (kind)
    {
    case RealtimeCaptureEvent::Kind::FileCreated:
        return "created";
    case RealtimeCaptureEvent::Kind::FileWritten:
        return "written";
    case RealtimeCaptureEvent::Kind::FileRenamed:
        return "renamed";
    case RealtimeCaptureEvent::Kind::FileDeleted:
        return "deleted";
    case RealtimeCaptureEvent::Kind::ValueSet:
        return "value set";
    case RealtimeCaptureEvent::Kind::ValueDeleted:
        return "value deleted";
    case RealtimeCaptureEvent::Kind::KeyDeleted:
        return "key deleted";
    }

    return "changed";
}

/**
 * @brief The text of a provider identifier, as the report spells it.
 * @param[in] guid The identifier.
 * @return The text of the identifier.
 */
std::string GuidText(const GUID& guid)
{
    return fmt::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", guid.Data1, guid.Data2,
                       guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4],
                       guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}

/**
 * @brief The identifier of the events of one kind of one provider.
 *
 * The identifier of an event is the one of its manifest for a manifest
 * provider and the event type of its opcode for a classic provider, and it is
 * only unique per provider and version, which is what the key carries.
 */
struct EventKey
{
    /** @brief The marker of the provider of the event. */
    unsigned provider = 0;

    /** @brief The identifier or the event type of the event. */
    unsigned short id = 0;

    /** @brief The version of the event. */
    unsigned char version = 0;

    /**
     * @brief Whether the events of the key come before the ones of another.
     * @param[in] other The key to compare with.
     * @return true when this key orders first.
     */
    bool operator<(const EventKey& other) const
    {
        if (provider != other.provider)
        {
            return provider < other.provider;
        }
        if (id != other.id)
        {
            return id < other.id;
        }
        return version < other.version;
    }
};

/**
 * @brief What a recording did with the events of one kind.
 */
struct EventCensus
{
    /** @brief Number of the events the provider delivered. */
    std::size_t received = 0;

    /** @brief Number of the events which became a change. */
    std::size_t emitted = 0;

    /** @brief Number of the events the noise tables left out. */
    std::size_t filtered = 0;

    /** @brief Number of the events which could not be resolved. */
    std::size_t unresolved = 0;

    /** @brief The reasons the events could not be resolved, with their counts. */
    std::map<UnresolvedReason, std::size_t> reasons;

    /** @brief The first events which could not be resolved, as text. */
    std::vector<std::string> details;

    /** @brief The properties the machine declares for the event. */
    std::vector<appbox::EtwEventProperty> properties;

    /**
     * @brief The level of the events which arrived.
     *
     * The level belongs to the event the kernel wrote and not to the manifest
     * the machine declares: a capture which reads no event of a kind the
     * manifest declares has to tell whether the level or the keyword of the
     * events which arrive differs from the ones of the declaration.
     */
    unsigned char level = 0;

    /** @brief The keyword of the events which arrived. */
    unsigned long long keyword = 0;

    /** @brief Whether the level and the keyword of the events were kept. */
    bool described = false;
};

/**
 * @brief The identifier of the events of one kind of another provider.
 *
 * The recording reads the events of two providers and counts the events of
 * every other provider it is delivered, so the report of a recording names what
 * it left out: a change a machine reports through another provider is the one
 * case a reader of a capture has to tell apart from a change the machine never
 * reported.
 */
struct IgnoredEventKey
{
    /** @brief The provider of the event. */
    GUID provider = {};

    /** @brief The identifier or the event type of the event. */
    unsigned short id = 0;

    /** @brief The version of the event. */
    unsigned char version = 0;

    /**
     * @brief Whether the events of the key come before the ones of another.
     * @param[in] other The key to compare with.
     * @return true when this key orders first.
     */
    bool operator<(const IgnoredEventKey& other) const
    {
        const auto left = std::memcmp(&provider, &other.provider, sizeof(GUID));
        if (left != 0)
        {
            return left < 0;
        }
        if (id != other.id)
        {
            return id < other.id;
        }
        return version < other.version;
    }
};

/**
 * @brief What a recording left out of one kind of another provider.
 */
struct IgnoredEventCensus
{
    /** @brief Number of the events which arrived. */
    std::size_t received = 0;

    /** @brief The name the machine gives the event, empty when it has none. */
    std::wstring name;
};

/**
 * @brief The kind of the events of one record.
 *
 * The identifier of an event is the one of its manifest for a manifest
 * provider and the event type of its opcode for a classic provider, whose
 * identifier is empty.
 *
 * @param[in] provider The marker of the provider of the event.
 * @param[in] record The event.
 * @return The kind of the event.
 */
EventKey EventKeyOf(unsigned provider, const EVENT_RECORD& record)
{
    EventKey key;
    key.provider = provider;
    key.id = record.EventHeader.EventDescriptor.Id != 0 ? record.EventHeader.EventDescriptor.Id
                                                        : record.EventHeader.EventDescriptor.Opcode;
    key.version = record.EventHeader.EventDescriptor.Version;
    return key;
}

/**
 * @brief Read the events a machine declares for the file provider.
 *
 * The table of the manifest of the provider is what the capture derives the
 * keywords of its enable from, so a machine whose manifest declares an event
 * with another keyword than the version the capture was written against is
 * still read. The call fails when the machine does not know the provider or
 * when its manifest cannot be read, which the caller reports and covers with
 * the keywords it knows.
 *
 * @param[out] events The events the machine declares.
 * @return true when the table was read.
 */
bool ReadFileProviderEvents(std::vector<appbox::ProviderEvent>& events)
{
    ULONG size = 0;
    auto  status = ::TdhEnumerateManifestProviderEvents(const_cast<GUID*>(&kKernelFileGuid), nullptr, &size);
    if (status != ERROR_INSUFFICIENT_BUFFER || size < sizeof(PROVIDER_EVENT_INFO))
    {
        return false;
    }

    std::vector<unsigned char> buffer(size);
    status = ::TdhEnumerateManifestProviderEvents(const_cast<GUID*>(&kKernelFileGuid),
                                                  reinterpret_cast<PROVIDER_EVENT_INFO*>(buffer.data()), &size);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }

    const auto* info = reinterpret_cast<const PROVIDER_EVENT_INFO*>(buffer.data());
    events.reserve(info->NumberOfEvents);
    for (ULONG index = 0; index < info->NumberOfEvents; ++index)
    {
        const auto&           descriptor = info->EventDescriptorsArray[index];
        appbox::ProviderEvent event;
        event.id = descriptor.Id;
        event.level = descriptor.Level;
        event.keyword = descriptor.Keyword;
        events.push_back(event);
    }

    return true;
}

/**
 * @brief The identifiers of the events of the file provider the capture reads.
 *
 * The identifier of an event belongs to the version of the manifest of the
 * machine: the kernel of a version of Windows numbers the events which report
 * the path of a file - the deletion, the rename and the file which came into
 * being - differently from the version the capture was written against, so the
 * capture asks the machine for the identifier of every event it reads by the
 * keyword the manifest of the machine declares for it. The keyword belongs to
 * the declaration of an event and not to its position in the manifest, and it
 * is what the kernel tags the event with.
 *
 * An event whose keyword the manifest of the machine does not declare is left
 * empty, which is what the caller reports: an event the capture cannot find is
 * a change it cannot read.
 */
struct FileEventIds
{
    /** @brief The Name event, which names the file of a file key. */
    unsigned short name = 0;

    /** @brief The NameRundown event, which names the files the kernel holds. */
    unsigned short name_rundown = 0;

    /** @brief The Create event, which opens or creates a file. */
    unsigned short create = 0;

    /** @brief The Write event, which writes to a file. */
    unsigned short write = 0;

    /** @brief The DeletePath event, which reports a deleted file. */
    unsigned short delete_path = 0;

    /** @brief The RenamePath event, which reports a renamed file. */
    unsigned short rename_path = 0;

    /** @brief The SetLinkPath event, which reports a linked file. */
    unsigned short set_link_path = 0;

    /** @brief The CreateNewFile event, which reports a file coming into being. */
    unsigned short create_new_file = 0;
};

/**
 * @brief The keywords the capture reads the events of the file provider by.
 *
 * The values are the ones the manifest of the provider declares for the events
 * the capture reads on the version of Windows the capture was written against.
 * The keyword of an event is what the kernel tags it with, so the capture finds
 * the event the machine numbers differently by the keyword it declares.
 */
constexpr unsigned long long kFileKeywordName = 0x10;
constexpr unsigned long long kFileKeywordCreate = 0xa0;
constexpr unsigned long long kFileKeywordWrite = 0x220;
constexpr unsigned long long kFileKeywordDeletePath = 0x400;
constexpr unsigned long long kFileKeywordRenamePath = 0x800;
constexpr unsigned long long kFileKeywordCreateNewFile = 0x1000;

/**
 * @brief The text of a name without the spaces around it.
 *
 * The metadata of a machine pads the name of an event, which the report of a
 * recording names.
 *
 * @param[in] name The name.
 * @return The name without the leading and trailing spaces.
 */
std::wstring Trimmed(const std::wstring& name)
{
    std::size_t begin = 0;
    std::size_t end = name.size();
    while (begin < end && ::iswspace(name[begin]) != 0)
    {
        ++begin;
    }
    while (end > begin && ::iswspace(name[end - 1]) != 0)
    {
        --end;
    }

    return name.substr(begin, end - begin);
}

/**
 * @brief Read the identifiers of the events the capture reads by their keyword.
 *
 * The keyword of an event is what the kernel tags it with and what the manifest
 * of the machine declares for it, so the capture finds the event a version of
 * Windows numbers differently by the keyword it declares. An event which shares
 * its keyword with another one - the two events which name a file, or the two
 * which report the path of a rename - is read as both of them, which the
 * recording resolves the same way.
 *
 * @param[in] events The events the machine declares for the file provider.
 * @return The identifiers of the events, zero for the events the manifest of
 *         the machine does not declare.
 */
FileEventIds ReadFileEventIds(const std::vector<appbox::ProviderEvent>& events)
{
    FileEventIds ids;

    for (const auto& event : events)
    {
        const auto keyword = event.keyword & 0xFFFFFFFFull;

        if (keyword == kFileKeywordName)
        {
            if (ids.name == 0)
            {
                ids.name = event.id;
            }
            else
            {
                ids.name_rundown = event.id;
            }
        }
        else if (keyword == kFileKeywordCreate)
        {
            ids.create = event.id;
        }
        else if (keyword == kFileKeywordWrite)
        {
            ids.write = event.id;
        }
        else if (keyword == kFileKeywordDeletePath)
        {
            ids.delete_path = event.id;
        }
        else if (keyword == kFileKeywordRenamePath)
        {
            if (ids.rename_path == 0)
            {
                ids.rename_path = event.id;
            }
            else
            {
                ids.set_link_path = event.id;
            }
        }
        else if (keyword == kFileKeywordCreateNewFile)
        {
            ids.create_new_file = event.id;
        }
    }

    return ids;
}

} // namespace

std::vector<unsigned short> RealtimeCaptureFileEventIds()
{
    return { kFileEventName,       kFileEventNameRundown, kFileEventCreate,      kFileEventWrite,
             kFileEventDeletePath, kFileEventRenamePath,  kFileEventSetLinkPath, kFileEventCreateNewFile };
}

ProviderEnable FileProviderEnableFrom(const std::vector<ProviderEvent>& events)
{
    ProviderEnable enable;

    /*
     * The keywords of every event the machine declares are joined: the
     * identifier of an event belongs to the version of the manifest of the
     * machine, so a capture which asked for the keywords of the events it reads
     * on the version it was written against would read no event whose
     * identifier that version numbers differently. The events the capture does
     * not read arrive as well and are left out where they are resolved.
     */
    for (const auto& event : events)
    {
        enable.keywords |= event.keyword;
    }

    for (const auto id : RealtimeCaptureFileEventIds())
    {
        const bool declared =
            std::any_of(events.begin(), events.end(), [id](const auto& event) { return event.id == id; });

        if (!declared)
        {
            enable.missing.push_back(id);
        }
    }

    return enable;
}

/**
 * @brief Implementation state of the recorder.
 *
 * The state owns the session, the property buffer behind it and the consumer
 * thread. The maps belong to the consumer thread alone, so they need no lock
 * of their own; the events and the finish flag are shared with the owner.
 */
struct RealtimeCaptureRecorder::State
{
    /** The noise tables of the recording. */
    RealtimeCaptureOptions options;

    /** The progress callback of the recording, may be empty. */
    RealtimeCaptureProgressCallback progress;

    /** The session property buffer, alive until the session was stopped. */
    std::vector<unsigned char> properties;

    /** Handle of the kernel logger session. */
    TRACEHANDLE session = 0;

    /** The consumer thread of the session. */
    std::thread consumer;

    /** Whether a stop was asked for, by the owner or by the callback. */
    std::atomic<bool> stop_requested{ false };

    /** Whether the consumer thread took the session. */
    std::atomic<bool> attached{ false };

    /** Whether the consumer thread left ProcessTrace. */
    std::atomic<bool> finished{ false };

    /** Guards the condition variable below. */
    std::mutex finish_mutex;

    /** Signals the end of the consumer thread to Join(). */
    std::condition_variable finish_condition;

    /** Guards the condition variable below. */
    std::mutex attach_mutex;

    /** Signals that the consumer thread took the session. */
    std::condition_variable attach_condition;

    /** Guards the counters below. */
    std::mutex statistics_mutex;

    /** The counters the session reported when it was stopped. */
    SessionStatistics statistics;

    /** Guards the events while the recording runs. */
    std::mutex events_mutex;

    /** The changes the filter of the options kept, in arrival order. */
    std::vector<RealtimeCaptureEvent> events;

    /** Registry key control blocks and the paths of their keys. */
    std::unordered_map<std::uint64_t, std::wstring> kcb_paths;

    /** File objects and the paths of their files. */
    std::unordered_map<std::uint64_t, std::wstring> file_paths;

    /** File keys of the kernel and the paths of their files. */
    std::unordered_map<std::uint64_t, std::wstring> file_key_paths;

    /** Process identifiers and the leaf names of their images. */
    std::unordered_map<std::uint32_t, std::wstring> process_names;

    /** Volume prefixes of the host and their drive letters. */
    std::vector<std::pair<std::wstring, std::wstring>> device_map;

    /** SID of the current user as text. */
    std::wstring current_sid;

    /** The decoder which reads the properties of the events by their name. */
    EtwEventDecoder decoder;

    /** The events the machine declares for the file provider, empty when its
     *  manifest could not be read. */
    std::vector<ProviderEvent> provider_events;

    /** The events the capture reads, by the identifier of the machine. */
    FileEventIds file_ids;

    /** The identifiers the machine gave the events, zero for the ones it did
     *  not name. */
    FileEventIds resolved_ids;

    /** The parameters the file provider was enabled with. */
    ProviderEnable provider_enable;

    /** What the recording did with the events of every kind it saw. */
    std::map<EventKey, EventCensus> census;

    /** The events of the providers the recording does not read, with counts. */
    std::map<IgnoredEventKey, IgnoredEventCensus> ignored;

    /** Number of the kinds of the ignored events the report named. */
    std::size_t ignored_named = 0;

    /** The volume prefixes the recording could not translate. */
    std::set<std::wstring> untranslated_prefixes;

    /** The first changes the recording reported, per kind. */
    std::map<RealtimeCaptureEvent::Kind, std::vector<std::string>> changes;

    /** The events a warning was reported for, with the reason of the warning. */
    std::set<std::pair<EventKey, UnresolvedReason>> warned;

    /** Number of the metadata tables the report named. */
    std::size_t metadata_named = 0;

    /** Events which could not be resolved to a path. */
    std::size_t unresolved = 0;

    /** Events recorded since the last progress report. */
    std::size_t since_report = 0;

    /** Time of the last progress report. */
    std::chrono::steady_clock::time_point last_report;

    /**
     * @brief Create the state with the session property buffer.
     */
    State() : properties(kPropertiesSize, 0)
    {
    }

    /**
     * @brief The leaf name of the image of a process.
     * @param[in] process_id Identifier of the process.
     * @return The leaf name, empty when it cannot be read.
     */
    const std::wstring& ProcessName(DWORD process_id)
    {
        auto& name = process_names[process_id];
        if (name.empty())
        {
            name = ProcessImageName(process_id);
        }
        return name;
    }

    /**
     * @brief Record one resolved change.
     *
     * The filter of the options is applied before the change is kept, and the
     * progress callback runs at a throttled rate.
     *
     * @param[in] event The change to record.
     */
    void Emit(const EventKey& key, RealtimeCaptureEvent&& event)
    {
        auto& entry = CensusOf(key);
        if (RealtimeCaptureEventExcluded(event, options))
        {
            ++entry.filtered;
            return;
        }

        ++entry.emitted;

        auto& named = changes[event.kind];
        if (named.size() < kChangeDetails)
        {
            named.push_back(DescribeChange(event));
        }

        RealtimeCaptureProgress report;
        {
            const std::lock_guard<std::mutex> lock(events_mutex);
            events.push_back(event);
            report.events = events.size();
        }
        report.current = event.path;
        report.process = event.process;

        ++since_report;
        const auto now = std::chrono::steady_clock::now();
        if (progress && (since_report >= kProgressEvents || now - last_report >= kProgressInterval))
        {
            since_report = 0;
            last_report = now;

            if (!progress(report))
            {
                /* The owner asked for the recording to end. */
                stop_requested.store(true);
            }
        }
    }

    /**
     * @brief The census of the events of one kind.
     * @param[in] key The kind of the events.
     * @return The census of that kind.
     */
    EventCensus& CensusOf(const EventKey& key)
    {
        return census[key];
    }

    /**
     * @brief Count one event which arrived.
     *
     * The level and the keyword of the first event of a kind are kept, because
     * the values belong to the event the kernel wrote: a capture which reads no
     * event of a kind the manifest of the machine declares has to tell whether
     * the level or the keyword of the events which arrive differs from the ones
     * of the declaration.
     *
     * @param[in] key The kind of the event.
     * @param[in] record The event.
     */
    void NoteReceived(const EventKey& key, const EVENT_RECORD& record)
    {
        auto& entry = CensusOf(key);
        ++entry.received;

        if (!entry.described)
        {
            entry.level = record.EventHeader.EventDescriptor.Level;
            entry.keyword = record.EventHeader.EventDescriptor.Keyword;
            entry.described = true;
        }
    }

    /**
     * @brief Count one event of a provider the recording does not read.
     *
     * The name the machine gives the event is kept for the first kinds of them,
     * which is what tells a reader whether the machine reports a change through
     * a provider the capture does not read.
     *
     * @param[in] record The event.
     */
    void NoteIgnored(const EVENT_RECORD& record)
    {
        IgnoredEventKey key;
        key.provider = record.EventHeader.ProviderId;
        key.id = record.EventHeader.EventDescriptor.Id != 0 ? record.EventHeader.EventDescriptor.Id
                                                            : record.EventHeader.EventDescriptor.Opcode;
        key.version = record.EventHeader.EventDescriptor.Version;

        auto& entry = ignored[key];
        ++entry.received;

        if (entry.name.empty() && ignored_named < kIgnoredDetails)
        {
            std::wstring opcode;
            std::wstring task;
            if (decoder.DescribeEventName(record, opcode, task))
            {
                if (opcode.empty())
                {
                    entry.name = Trimmed(task);
                }
                else if (task.empty())
                {
                    entry.name = Trimmed(opcode);
                }
                else
                {
                    entry.name = Trimmed(opcode) + L"/" + Trimmed(task);
                }

                ++ignored_named;
            }
        }
    }

    /**
     * @brief Note that an event of one kind could not be resolved.
     *
     * The reason is counted for the kind of the event, the first events of the
     * kind are described and the metadata the machine declares for the event is
     * kept, which is what the report of the recording names: a reader sees what
     * the machine declares for the event and what the event carried. The
     * warning is reported once per kind and reason, because a recording
     * observes the whole machine and an event which cannot be resolved is the
     * rule and not the exception.
     *
     * @param[in] key The kind of the event.
     * @param[in] reason The reason the event was left out.
     * @param[in] detail The description of the event.
     * @param[in] record The event itself, null when it is not at hand.
     */
    void NoteUnresolved(const EventKey& key, UnresolvedReason reason, std::string detail,
                        const EVENT_RECORD* record = nullptr)
    {
        auto& entry = CensusOf(key);
        ++entry.unresolved;
        ++entry.reasons[reason];

        if (entry.details.size() < kUnresolvedDetails)
        {
            entry.details.push_back(std::move(detail));
        }

        if (record != nullptr && entry.properties.empty() && metadata_named < kMetadataDetails)
        {
            if (decoder.DescribeProperties(*record, entry.properties))
            {
                ++metadata_named;
            }
        }

        ++unresolved;

        if (!warned.insert({ key, reason }).second)
        {
            return;
        }

        spdlog::warn("the realtime capture left an event of provider {} (id {}) unresolved: {}", key.provider, key.id,
                     UnresolvedReasonName(reason));
    }

    /**
     * @brief The first change of a kind, as the report of a recording spells it.
     * @param[in] event The change.
     * @return The description of the change.
     */
    static std::string DescribeChange(const RealtimeCaptureEvent& event)
    {
        std::string text =
            fmt::format("{} {} by {}", ChangeKindName(event.kind), WideToUTF8(event.path), WideToUTF8(event.process));
        if (!event.detail.empty())
        {
            text += fmt::format(" -> {}", WideToUTF8(event.detail));
        }
        return text;
    }

    /**
     * @brief The payload of an event as text, for the report.
     *
     * The bytes are what a reader needs when the machine does not declare the
     * metadata of an event: the layout of the payload is the only thing which
     * is left, and the first bytes of an event carry the object and the name of
     * its path.
     *
     * @param[in] record The event.
     * @return The first bytes of the payload as hexadecimal text.
     */
    static std::string DescribePayload(const EVENT_RECORD& record)
    {
        const auto* bytes = static_cast<const unsigned char*>(record.UserData);
        const auto  length = static_cast<std::size_t>(record.UserDataLength);

        std::string text;
        const auto  count = length < kPayloadDetails ? length : kPayloadDetails;
        for (std::size_t index = 0; index < count; ++index)
        {
            text += fmt::format("{:02x}", bytes[index]);
        }
        if (count < length)
        {
            text += fmt::format("... of {} bytes", length);
        }
        return text;
    }

    /**
     * @brief The fields of a file event as the report of a recording spells them.
     * @param[in] record The event.
     * @param[in] fields The fields which were read of the event.
     * @return The description of the event.
     */
    std::string DescribeFileFields(const EVENT_RECORD& record, const EtwEventFields& fields)
    {
        const auto process_id = record.EventHeader.ProcessId;

        std::string text = fmt::format("pid={} process={} object=0x{:x} file_key=0x{:x} name={}", process_id,
                                       WideToUTF8(ProcessName(process_id)), fields.object, fields.file_key,
                                       fields.has_name ? WideToUTF8(fields.name) : std::string("<none>"));
        if (!fields.complete)
        {
            text += fmt::format(" walk_stopped_at={}", fields.stopped_at);
        }

        text += fmt::format(" payload={}", DescribePayload(record));
        return text;
    }

    /**
     * @brief Translate a registry event into a change and record it.
     * @param[in] key The kind of the event.
     * @param[in] kind The kind of the change.
     * @param[in] process_id The process which caused the event.
     * @param[in] time The time of the event.
     * @param[in] kcb The key control block the event names.
     * @param[in] value_name The name of the value, empty for a key event.
     */
    void EmitRegistry(const EventKey& key, RealtimeCaptureEvent::Kind kind, DWORD process_id, std::int64_t time,
                      std::uint64_t kcb, std::wstring value_name)
    {
        const auto path = kcb_paths.find(kcb);
        if (path == kcb_paths.end())
        {
            NoteUnresolved(key, UnresolvedReason::UnknownObject,
                           fmt::format("pid={} process={} kcb=0x{:x} value={}", process_id,
                                       WideToUTF8(ProcessName(process_id)), kcb, WideToUTF8(value_name)));
            return;
        }

        const auto view = NtRegistryPathToViewPath(path->second, current_sid);
        if (view.empty())
        {
            NoteUnresolved(key, UnresolvedReason::NotInView,
                           fmt::format("pid={} process={} nt={} value={}", process_id,
                                       WideToUTF8(ProcessName(process_id)), WideToUTF8(path->second),
                                       WideToUTF8(value_name)));
            return;
        }

        RealtimeCaptureEvent event;
        event.kind = kind;
        event.process = ProcessName(process_id);
        event.path = view;
        event.detail = std::move(value_name);
        event.time = time;
        Emit(key, std::move(event));
    }

    /**
     * @brief Record a file object path for the events which follow.
     * @param[in] file_object The file object of the event.
     * @param[in] path The Win32 path of the file.
     */
    void RememberFile(std::uint64_t file_object, const std::wstring& path)
    {
        if (file_paths.size() >= kMaxFileObjects)
        {
            spdlog::warn("the realtime capture dropped its file map of {} objects", file_paths.size());
            file_paths.clear();
        }
        file_paths[file_object] = path;
    }

    /**
     * @brief Record the path of a file key for the events which follow.
     * @param[in] file_key The file key of the kernel.
     * @param[in] path The Win32 path of the file.
     */
    void RememberFileKey(std::uint64_t file_key, const std::wstring& path)
    {
        if (file_key_paths.size() >= kMaxFileObjects)
        {
            spdlog::warn("the realtime capture dropped its file key map of {} objects", file_key_paths.size());
            file_key_paths.clear();
        }
        file_key_paths[file_key] = path;
    }

    /**
     * @brief The path of a file the recording saw.
     *
     * The file object of the kernel is the object a handle names, which is
     * what ties the events of one open file together. A file which was opened
     * before the recording began is named by the rundown of its file key, so
     * the key is the second chance to resolve the event of a file.
     *
     * @param[in] file_object The file object of the event.
     * @param[in] file_key The file key of the event, zero when it carries none.
     * @return The Win32 path of the file, empty when the recording never saw it.
     */
    std::wstring KnownFilePath(std::uint64_t file_object, std::uint64_t file_key) const
    {
        const auto object = file_paths.find(file_object);
        if (object != file_paths.end())
        {
            return object->second;
        }

        const auto key = file_key_paths.find(file_key);
        return key == file_key_paths.end() ? std::wstring() : key->second;
    }

    /**
     * @brief Record a change of a file.
     * @param[in] kind The kind of the change.
     * @param[in] process_id The process which caused the event.
     * @param[in] time The time of the event.
     * @param[in] path The Win32 path of the file.
     */
    void EmitFile(const EventKey& key, RealtimeCaptureEvent::Kind kind, DWORD process_id, std::int64_t time,
                  std::wstring path)
    {
        RealtimeCaptureEvent event;
        event.kind = kind;
        event.process = ProcessName(process_id);
        event.path = std::move(path);
        event.time = time;
        Emit(key, std::move(event));
    }

    /**
     * @brief The event callback of the consumer thread.
     *
     * The callback is a static member so it can reach the private state of the
     * recorder, and it hands the events of the two providers the capture
     * subscribed to to the decoder, which reads their properties by name, see
     * `docs/Capture.md`.
     *
     * @param[in] record The event the kernel delivered.
     */
    static VOID WINAPI OnKernelEvent(PEVENT_RECORD record)
    {
        auto* self = static_cast<State*>(record->UserContext);
        if (self == nullptr)
        {
            return;
        }

        if (EtwEventDecoder::IsWppEvent(*record))
        {
            /*
             * The capture subscribes to the providers of the kernel, which
             * write the events of a manifest and never a WPP message, so a
             * message is counted instead of being decoded.
             */
            self->NoteUnresolved(EventKeyOf(kWppProvider, *record), UnresolvedReason::WppMessage,
                                 self->DescribePayload(*record), record);
            return;
        }

        if (SameGuid(record->EventHeader.ProviderId, kKernelFileGuid))
        {
            self->OnFileEvent(*record);
            return;
        }

        if (SameGuid(record->EventHeader.ProviderId, kRegistryGuid))
        {
            self->OnRegistryEvent(*record);
            return;
        }

        /*
         * An event of another provider is left out, and the count of what was
         * left out is what the report of the recording names: a machine which
         * reports a change through a provider the capture does not read is the
         * one case a reader has to tell apart from a machine which reports no
         * change at all.
         */
        self->NoteIgnored(*record);
    }

private:
    /**
     * @brief Resolve one registry event of the classic provider of the kernel.
     *
     * The events name the key control block of their key, and the table of the
     * recording turns the block into the path of the key. The rundown which
     * the kernel reports when the session starts fills that table with the
     * keys which exist, so a value which is written to a key that was opened
     * before the recording began is resolved as well. The classic provider is
     * the source of the registry events for the reason the constant of its
     * GUID names.
     *
     * @param[in] record The event record.
     */
    void OnRegistryEvent(const EVENT_RECORD& record)
    {
        const auto key = EventKeyOf(kRegistryProvider, record);
        NoteReceived(key, record);

        EtwEventFields fields;
        if (!decoder.Read(record, fields))
        {
            NoteUnresolved(key, UnresolvedReason::UnknownProvider, DescribePayload(record), &record);
            return;
        }

        const auto process_id = record.EventHeader.ProcessId;
        const auto time = record.EventHeader.TimeStamp.QuadPart;

        const auto opcode = static_cast<unsigned short>(record.EventHeader.EventDescriptor.Opcode);

        switch (opcode)
        {
        case kRegOpKcbRundown:
        case kRegOpKcbCreate:
            /*
             * The rundown and the create carry the block of the key and the
             * path of it. The kernel reports the path of a block in the
             * spelling it holds, which is the path of the view for the blocks
             * of a hive and a name which is relative to the hive of the key
             * for the others; only the paths of the view are kept, because a
             * name which is relative to a hive cannot be turned into one.
             */
            if (fields.has_object && fields.has_name && !NtRegistryPathToViewPath(fields.name, current_sid).empty())
            {
                kcb_paths[fields.object] = std::move(fields.name);
            }
            return;
        case kRegOpKcbDelete:
            if (fields.has_object)
            {
                kcb_paths.erase(fields.object);
            }
            return;
        case kRegOpSetValue:
            EmitRegistry(key, RealtimeCaptureEvent::Kind::ValueSet, process_id, time, fields.object,
                         std::move(fields.name));
            return;
        case kRegOpDeleteValue:
            EmitRegistry(key, RealtimeCaptureEvent::Kind::ValueDeleted, process_id, time, fields.object,
                         std::move(fields.name));
            return;
        case kRegOpDeleteKey:
            EmitRegistry(key, RealtimeCaptureEvent::Kind::KeyDeleted, process_id, time, fields.object, std::wstring());
            return;
        default:
            return;
        }
    }

    /**
     * @brief Resolve one file event of the manifest provider of the kernel.
     *
     * The properties are read by their name, so the version of the machine
     * cannot change the meaning of an event: the event which reports that a
     * file came into being is the create new file event, which is the signal
     * of a new entry, and the write of a file is the signal of a change of an
     * entry which was there before. The events themselves are read by the name
     * the manifest of the machine gives them as well, because the identifier of
     * an event belongs to the version of that manifest, see ReadFileEventIds().
     *
     * @param[in] record The event record.
     */
    void OnFileEvent(const EVENT_RECORD& record)
    {
        const auto key = EventKeyOf(kFileProvider, record);
        NoteReceived(key, record);

        EtwEventFields fields;
        if (!decoder.Read(record, fields))
        {
            NoteUnresolved(key, UnresolvedReason::UnknownProvider, DescribePayload(record), &record);
            return;
        }

        const auto process_id = record.EventHeader.ProcessId;
        const auto time = record.EventHeader.TimeStamp.QuadPart;
        const auto id = key.id;

        if (id == file_ids.name || id == file_ids.name_rundown)
        {
            /* The rundown names the file of a file key, which is what ties the
             * write of a file which was opened before the recording to it. */
            if (fields.has_file_key && fields.has_name)
            {
                const auto path = NtFilePathToWin32Path(fields.name, device_map);
                if (path.empty())
                {
                    NoteUntranslated(fields.name);
                }
                else
                {
                    RememberFileKey(fields.file_key, path);
                }
            }
            return;
        }
        if (id == file_ids.create || id == file_ids.create_new_file)
        {
            if (!fields.has_object)
            {
                NoteUnresolved(key, UnresolvedReason::NoObject, DescribeFileFields(record, fields), &record);
                return;
            }

            if (!fields.has_name)
            {
                NoteUnresolved(key, UnresolvedReason::NoName, DescribeFileFields(record, fields), &record);
                return;
            }

            const auto path = NtFilePathToWin32Path(fields.name, device_map);
            if (path.empty())
            {
                NoteUntranslated(fields.name);
                NoteUnresolved(key, UnresolvedReason::UntranslatedPath, DescribeFileFields(record, fields), &record);
                return;
            }

            RememberFile(fields.object, path);
            if (fields.has_file_key)
            {
                RememberFileKey(fields.file_key, path);
            }
            if (id == file_ids.create_new_file)
            {
                EmitFile(key, RealtimeCaptureEvent::Kind::FileCreated, process_id, time, path);
            }
            return;
        }

        if (id == file_ids.write)
        {
            const auto path = KnownFilePath(fields.object, fields.file_key);
            if (path.empty())
            {
                NoteUnresolved(key, UnresolvedReason::UnknownObject, DescribeFileFields(record, fields), &record);
                return;
            }

            EmitFile(key, RealtimeCaptureEvent::Kind::FileWritten, process_id, time, path);
            return;
        }

        if (id == file_ids.delete_path)
        {
            if (!fields.has_name)
            {
                NoteUnresolved(key, UnresolvedReason::NoName, DescribeFileFields(record, fields), &record);
                return;
            }

            const auto path = NtFilePathToWin32Path(fields.name, device_map);
            if (path.empty())
            {
                NoteUntranslated(fields.name);
                NoteUnresolved(key, UnresolvedReason::UntranslatedPath, DescribeFileFields(record, fields), &record);
                return;
            }

            file_paths.erase(fields.object);
            EmitFile(key, RealtimeCaptureEvent::Kind::FileDeleted, process_id, time, path);
            return;
        }

        if (id == file_ids.rename_path || id == file_ids.set_link_path)
        {
            const auto target = fields.has_name ? NtFilePathToWin32Path(fields.name, device_map) : std::wstring();
            const auto old_path = KnownFilePath(fields.object, fields.file_key);
            if (target.empty() || old_path.empty())
            {
                if (fields.has_name && target.empty())
                {
                    NoteUntranslated(fields.name);
                    NoteUnresolved(key, UnresolvedReason::UntranslatedPath, DescribeFileFields(record, fields),
                                   &record);
                }
                else if (!fields.has_name)
                {
                    NoteUnresolved(key, UnresolvedReason::NoName, DescribeFileFields(record, fields), &record);
                }
                else
                {
                    NoteUnresolved(key, UnresolvedReason::UnknownObject, DescribeFileFields(record, fields), &record);
                }
                return;
            }

            file_paths.erase(fields.object);

            RealtimeCaptureEvent event;
            event.kind = RealtimeCaptureEvent::Kind::FileRenamed;
            event.process = ProcessName(process_id);
            event.path = old_path;
            event.detail = target;
            event.time = time;
            Emit(key, std::move(event));

            RememberFile(fields.object, target);
            if (fields.has_file_key)
            {
                RememberFileKey(fields.file_key, target);
            }
            return;
        }
    }

    /**
     * @brief Note that a path names no volume of the host.
     *
     * The prefix of the path is what tells a reader which device the events of
     * that kind name: the events of a named pipe or of a network share are not
     * part of the host, while a volume the recording cannot resolve is the one
     * case a capture has to report.
     *
     * @param[in] nt_path The path the kernel reported.
     */
    void NoteUntranslated(const std::wstring& nt_path)
    {
        /*
         * A path of the kernel names its device first (`\Device\HarddiskVolume4`)
         * and the name of the volume behind it, so the prefix of the report is
         * the device together with the volume it names and not the `\Device`
         * every path of the kernel carries.
         */
        static const std::wstring kDevicePrefix = L"\\Device\\";

        std::size_t begin = 0;
        if (nt_path.compare(0, kDevicePrefix.size(), kDevicePrefix) == 0)
        {
            begin = kDevicePrefix.size();
        }

        const auto separator = nt_path.find(L'\\', begin);
        if (separator == std::wstring::npos)
        {
            /*
             * The path names a device of the machine and no entry below it: the
             * volumes themselves are opened while a recording runs, which is not
             * a path the capture has to translate.
             */
            return;
        }

        untranslated_prefixes.insert(nt_path.substr(0, separator));
    }

public:
    /**
     * @brief Report the events the capture reads which the machine does not
     *        declare.
     *
     * The capture resolves the events it reads by the keyword the manifest of
     * the machine declares for them, so a keyword the machine does not declare
     * is an event the recording cannot read. The warning names the events of
     * that kind instead of leaving the gap to the report of the recording
     * alone.
     */
    void ReportFileEventNames() const
    {
        const std::pair<const char*, unsigned short> named[] = {
            { "Name",          resolved_ids.name            },
            { "NameRundown",   resolved_ids.name_rundown    },
            { "Create",        resolved_ids.create          },
            { "Write",         resolved_ids.write           },
            { "DeletePath",    resolved_ids.delete_path     },
            { "RenamePath",    resolved_ids.rename_path     },
            { "SetLinkPath",   resolved_ids.set_link_path   },
            { "CreateNewFile", resolved_ids.create_new_file },
        };

        std::string missing;
        for (const auto& entry : named)
        {
            if (entry.second != 0)
            {
                continue;
            }

            if (!missing.empty())
            {
                missing += ", ";
            }
            missing += entry.first;
        }

        if (!missing.empty())
        {
            spdlog::warn("the file provider of this machine declares no keyword the capture reads for {}", missing);
        }
    }

    /**
     * @brief Write the report of the recording.
     *
     * The report is what a reader of a capture needs when a change is missing:
     * the counters of the session, the volumes of the host, the events the
     * machine declares for the file provider, what the recording did with the
     * events of every kind and the changes it reported. It is written at the
     * debug level, so a run of the packer is not flooded with it while the log
     * of a case which failed carries it.
     */
    void Report()
    {
        ReportSession();
        ReportVolumes();
        ReportProvider();
        ReportCensus();
        ReportIgnored();
        ReportChanges();
    }

    /**
     * @brief Write the counters the session of the recording reported.
     */
    void ReportSession()
    {
        SessionStatistics reported;
        {
            const std::lock_guard<std::mutex> lock(statistics_mutex);
            reported = statistics;
        }

        if (!reported.available)
        {
            spdlog::debug("realtime capture report: the session did not report its counters");
            return;
        }

        spdlog::debug("realtime capture report: the session held {} buffers and {} of them were free when it stopped",
                      reported.number_of_buffers, reported.free_buffers);
        spdlog::debug("realtime capture report: the session dropped {} events, {} real time buffers and {} log buffers",
                      reported.events_lost, reported.real_time_buffers_lost, reported.log_buffers_lost);
    }

    /**
     * @brief Write the volumes of the host and the devices which did not match.
     */
    void ReportVolumes()
    {
        if (device_map.empty())
        {
            spdlog::debug("realtime capture report: the volumes of the host could not be resolved");
        }

        for (const auto& device : device_map)
        {
            spdlog::debug("realtime capture report: the volume {} is the drive {}", WideToUTF8(device.first),
                          WideToUTF8(device.second));
        }

        for (const auto& prefix : untranslated_prefixes)
        {
            spdlog::debug("realtime capture report: no volume of the host matches the device {}", WideToUTF8(prefix));
        }
    }

    /**
     * @brief Write the events the machine declares for the file provider.
     *
     * The table names the events the capture reads together with the level and
     * the keyword the machine declares them with: an event of that list which
     * the census below does not hold is an event the machine declares and never
     * delivered, which is what a capture has to tell apart from an event it
     * could not resolve.
     */
    void ReportProvider()
    {
        if (provider_events.empty())
        {
            spdlog::debug("realtime capture report: the manifest of the file provider of the machine was not read");
        }

        for (const auto id : RealtimeCaptureFileEventIds())
        {
            bool declared = false;
            for (const auto& event : provider_events)
            {
                if (event.id != id)
                {
                    continue;
                }

                declared = true;
                spdlog::debug("realtime capture report: the machine declares the file event {} with the level {} and "
                              "the keyword 0x{:x}",
                              event.id, event.level, event.keyword);
            }

            if (!declared)
            {
                spdlog::debug("realtime capture report: the machine declares no file event {}", id);
            }
        }

        spdlog::debug("realtime capture report: the file provider was enabled with the keyword 0x{:x}",
                      provider_enable.keywords);

        /*
         * The events the capture reads are the ones the machine names, so the
         * report names the identifier of every one of them: a reader sees which
         * event of the machine the capture read for which name, and an event
         * the machine declares and never delivered is the one case to tell
         * apart from an event the recording could not resolve.
         */
        const std::pair<const char*, unsigned short> reads[] = {
            { "Name",          file_ids.name            },
            { "NameRundown",   file_ids.name_rundown    },
            { "Create",        file_ids.create          },
            { "Write",         file_ids.write           },
            { "DeletePath",    file_ids.delete_path     },
            { "RenamePath",    file_ids.rename_path     },
            { "SetLinkPath",   file_ids.set_link_path   },
            { "CreateNewFile", file_ids.create_new_file },
        };
        const std::pair<const char*, unsigned short> named[] = {
            { "Name",          resolved_ids.name            },
            { "NameRundown",   resolved_ids.name_rundown    },
            { "Create",        resolved_ids.create          },
            { "Write",         resolved_ids.write           },
            { "DeletePath",    resolved_ids.delete_path     },
            { "RenamePath",    resolved_ids.rename_path     },
            { "SetLinkPath",   resolved_ids.set_link_path   },
            { "CreateNewFile", resolved_ids.create_new_file },
        };

        for (std::size_t index = 0; index < std::size(reads); ++index)
        {
            const auto id = reads[index].second;
            const bool received = std::any_of(census.begin(), census.end(), [id](const auto& item) {
                return item.first.provider == kFileProvider && item.first.id == id;
            });

            spdlog::debug("realtime capture report: the capture read the file event {} as the event {} of the {}",
                          reads[index].first, id,
                          named[index].second != 0 ? "keyword the machine declares"
                                                   : "version the capture was written against");

            if (!received)
            {
                spdlog::debug("realtime capture report: the recording received no file event {} at all",
                              reads[index].first);
            }
        }
    }

    /**
     * @brief Write the events of the providers the recording does not read.
     *
     * The recording reads the events of two providers, and the events of every
     * other provider are counted and named: a machine which reports a change
     * through a provider the capture does not read is the one case a reader has
     * to tell apart from a machine which reports no change at all.
     */
    void ReportIgnored()
    {
        for (const auto& item : ignored)
        {
            const auto& key = item.first;
            const auto& entry = item.second;

            const std::string named =
                entry.name.empty() ? std::string() : " (the machine names it " + WideToUTF8(entry.name) + ")";
            spdlog::debug("realtime capture report: the provider {} event {} version {} was received {} times and left "
                          "out{}",
                          GuidText(key.provider), key.id, key.version, entry.received, named);
        }
    }

    /**
     * @brief Write what the recording did with the events of every kind.
     *
     * A kind the recording never saw is not part of the report: the table is
     * what the recording made of the events of the machine, and the events the
     * machine declares but never delivered are named by the table of the
     * provider above.
     */
    void ReportCensus()
    {
        for (const auto& item : census)
        {
            const auto& key = item.first;
            const auto& entry = item.second;

            std::string reasons;
            for (const auto& reason : entry.reasons)
            {
                if (!reasons.empty())
                {
                    reasons += ", ";
                }
                reasons += fmt::format("{} x{}", UnresolvedReasonName(reason.first), reason.second);
            }

            const char* opening = reasons.empty() ? "" : " (";
            const char* closing = reasons.empty() ? "" : ")";

            spdlog::debug("realtime capture report: provider {} event {} version {} was received {} times with the "
                          "level {} and the keyword 0x{:x}, emitted {}, filtered {}, unresolved {}{}{}{}",
                          key.provider, key.id, key.version, entry.received, entry.level, entry.keyword, entry.emitted,
                          entry.filtered, entry.unresolved, opening, reasons, closing);

            for (const auto& detail : entry.details)
            {
                spdlog::debug("realtime capture report: provider {} event {} version {} carried {}", key.provider,
                              key.id, key.version, detail);
            }

            const auto described =
                entry.properties.size() < kPropertyDetails ? entry.properties.size() : kPropertyDetails;
            for (std::size_t index = 0; index < described; ++index)
            {
                const auto& property = entry.properties[index];
                spdlog::debug(
                    "realtime capture report: provider {} event {} version {} declares the property {} of the "
                    "type {} with the flags 0x{:x}, the count {} and the length {}",
                    key.provider, key.id, key.version, WideToUTF8(property.name), property.in_type, property.flags,
                    property.count, property.length);
            }
        }
    }

    /**
     * @brief Write the changes the recording reported.
     */
    void ReportChanges()
    {
        for (const auto& item : changes)
        {
            for (const auto& change : item.second)
            {
                spdlog::debug("realtime capture report: the recording reported the {} entry {}",
                              ChangeKindName(item.first), change);
            }
        }
    }
};

RealtimeCaptureRecorder::RealtimeCaptureRecorder() = default;

RealtimeCaptureRecorder::~RealtimeCaptureRecorder()
{
    if (state_ != nullptr)
    {
        RequestStop();
        (void)Join(10000);
    }
}

bool RealtimeCaptureRecorder::GiveUpFinishedRecording()
{
    if (state_ == nullptr)
    {
        return true;
    }

    /*
     * The consumer thread is the one which reads the events of the session, so
     * a recording whose consumer still runs is a recording which runs: its
     * state may not be dropped and the kernel logger may not be taken over.
     */
    if (state_->consumer.joinable() && !state_->finished.load())
    {
        return false;
    }

    /*
     * The session is given back before the state is dropped: a consumer which
     * left on its own, which is what a trace that cannot be opened does, keeps
     * a session which still runs and would hold the kernel logger of the
     * machine for a recording nobody reads.
     */
    RequestStop();
    if (state_->consumer.joinable())
    {
        state_->consumer.join();
    }

    spdlog::info("the realtime capture gave up the recording which ended");
    state_.reset();
    return true;
}

bool RealtimeCaptureRecorder::Start(const RealtimeCaptureOptions&          options,
                                    const RealtimeCaptureProgressCallback& progress, std::string& error)
{
    /*
     * The state of a recording which ended is given up here, so a capture
     * which follows a cancelled or a confirmed one finds an idle recorder and
     * the kernel logger of the machine is free again.
     */
    if (!GiveUpFinishedRecording())
    {
        error = "a recording is already running";
        return false;
    }

    if (!RealtimeCaptureSupported())
    {
        error = "the realtime capture needs Windows 10 or newer; use the snapshot capture instead";
        return false;
    }
    if (!IsProcessElevated())
    {
        error = "the realtime capture needs an elevated process";
        return false;
    }

    auto state = std::make_unique<State>();
    state->options = options;
    state->progress = progress;
    state->device_map = BuildDeviceMap();
    state->current_sid = CurrentUserSid();
    state->last_report = std::chrono::steady_clock::now();

    if (state->device_map.empty())
    {
        /*
         * The paths of the kernel name the volumes of the machine, so a host
         * whose volumes cannot be resolved leaves every file event of the
         * recording without a path. The report names the reason instead of
         * dropping the events without a trace.
         */
        spdlog::warn("the realtime capture could not resolve the volumes of the host");
    }

    auto* properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(state->properties.data());
    properties->Wnode.BufferSize = static_cast<unsigned long>(kPropertiesSize);
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    properties->BufferSize = kSessionBufferSize;
    properties->MinimumBuffers = kSessionMinimumBuffers;
    properties->MaximumBuffers = kSessionMaximumBuffers;
    properties->MaximumFileSize = kSessionMaximumFileSize;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    properties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024 * sizeof(wchar_t);
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;

    /*
     * The registry group is enabled with the flags of the session and not with
     * an enable of the system trace provider of the session, which a private
     * system logger session does not answer: the call is accepted and no event
     * of the group arrives. The kernel reports the rundown of the blocks of the
     * keys it holds when the group is enabled, which is the moment the session
     * starts, so that burst arrives before the consumer took the session and
     * the buffers of the session are sized to carry it.
     *
     * The file groups are enabled with the flags of the session as well. The
     * manifest provider of the file events declares the events the capture
     * reads with keywords of its own, but a version of the kernel decides
     * whether it writes the events which report the path of a file - the
     * deletion, the rename and the file which came into being - from the
     * classic groups of the file I/O of the session, so a capture which asked
     * for the manifest keywords alone would read no such event on that machine.
     * The classic events of the file provider arrive as well and are left out
     * by the recording, which reads the events of the manifest provider.
     */
    properties->EnableFlags = EVENT_TRACE_FLAG_REGISTRY | EVENT_TRACE_FLAG_FILE_IO | EVENT_TRACE_FLAG_FILE_IO_INIT;

    /*
     * The session of the capture is a system logger session of its own: the
     * manifest providers of the kernel answer only to one, and the legacy
     * kernel logger of the machine refuses them. The control GUID of the
     * legacy logger is left empty, which is what makes the session a private
     * one; a leftover session of a crashed run is stopped here, because it
     * would keep the next recording from starting.
     */
    auto status = ::StartTraceW(&state->session, kSessionName, properties);
    if (status == ERROR_ALREADY_EXISTS)
    {
        status = ::ControlTraceW(0, kSessionName, properties, EVENT_TRACE_CONTROL_STOP);
        spdlog::info("the realtime capture stopped a leftover session of its own: {}",
                     static_cast<unsigned long>(status));
        status = ::StartTraceW(&state->session, kSessionName, properties);
    }
    if (status != ERROR_SUCCESS)
    {
        error = "the session of the capture could not be started (error " +
                std::to_string(static_cast<unsigned long>(status)) + ")";
        return false;
    }

    state_ = std::move(state);

    /*
     * The consumer takes the session before a provider is enabled: the events
     * of a provider are carried by the buffers of the session until the
     * consumer drains them, and a burst which fills the buffers while nothing
     * consumes the session is dropped by the kernel. The registry rundown is
     * such a burst, and it arrives right when its group is enabled, so the
     * providers are enabled only once the consumer runs.
     */
    state_->consumer = std::thread([this]() { Consume(); });

    if (!WaitForConsumerAttached(kConsumerAttachMilliseconds))
    {
        error = "the consumer of the session did not take the recording";
        GiveUpState();
        return false;
    }

    if (!EnableFileProvider(error))
    {
        GiveUpState();
        return false;
    }

    return true;
}

void RealtimeCaptureRecorder::Consume()
{
    EVENT_TRACE_LOGFILEW trace = {};
    trace.LoggerName = const_cast<LPWSTR>(kSessionName);
    trace.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    trace.EventRecordCallback = State::OnKernelEvent;
    trace.Context = state_.get();

    const auto handle = ::OpenTraceW(&trace);
    if (handle == INVALID_PROCESSTRACE_HANDLE)
    {
        spdlog::error("the realtime capture could not open its trace: {}",
                      static_cast<unsigned long>(::GetLastError()));
        FinishConsumer();
        return;
    }

    /*
     * The session is taken from here on: every event a provider writes is
     * drained by the ProcessTrace() below, which is what the owner waits for
     * before it enables a provider.
     */
    SignalAttached();

    TRACEHANDLE handles[] = { handle };
    (void)::ProcessTrace(handles, 1, nullptr, nullptr);
    ::CloseTrace(handle);

    FinishConsumer();
}

void RealtimeCaptureRecorder::SignalAttached()
{
    {
        const std::lock_guard<std::mutex> lock(state_->attach_mutex);
        state_->attached.store(true);
    }
    state_->attach_condition.notify_all();
}

void RealtimeCaptureRecorder::FinishConsumer()
{
    state_->Report();

    if (state_->unresolved > 0)
    {
        spdlog::warn("the realtime capture could not resolve {} events", state_->unresolved);
    }

    state_->finished.store(true);
    {
        const std::lock_guard<std::mutex> lock(state_->finish_mutex);
        state_->finish_condition.notify_all();
    }

    /* A start which waits for the consumer is woken up as well: a consumer
     * which leaves on its own never took the session. */
    state_->attach_condition.notify_all();
}

bool RealtimeCaptureRecorder::WaitForConsumerAttached(unsigned milliseconds)
{
    if (state_ == nullptr)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(state_->attach_mutex);
    (void)state_->attach_condition.wait_for(lock, std::chrono::milliseconds(milliseconds),
                                            [this]() { return state_->attached.load() || state_->finished.load(); });
    lock.unlock();

    return state_->attached.load();
}

bool RealtimeCaptureRecorder::EnableFileProvider(std::string& error)
{
    /*
     * The keywords and the level of the file provider are the ones the manifest
     * of the machine declares for the events the capture reads, so a machine
     * whose manifest declares them with another keyword or another level is
     * still read. A machine whose manifest cannot be read is asked for the
     * keywords of the version the capture was written against.
     */
    std::vector<ProviderEvent> events;
    if (!ReadFileProviderEvents(events))
    {
        spdlog::warn("the realtime capture could not read the manifest of the file provider of the machine");
        const auto enabled = ::EnableTraceEx2(state_->session, &kKernelFileGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                              TRACE_LEVEL_INFORMATION, kKernelFileKeywords, 0, 0, nullptr);
        if (enabled != ERROR_SUCCESS)
        {
            error = "the file provider of the kernel could not be enabled (error " +
                    std::to_string(static_cast<unsigned long>(enabled)) + ")";
            return false;
        }
        return true;
    }

    const auto enable = FileProviderEnableFrom(events);
    for (const auto id : enable.missing)
    {
        spdlog::warn("the file provider of this machine declares no event with the identifier {} the capture reads",
                     id);
    }

    auto keywords = enable.keywords;
    if (keywords == 0)
    {
        /* A manifest which declares no keyword cannot be asked for its events
         * one by one, so the provider is asked for every keyword. */
        spdlog::warn("the file provider of this machine declares no keyword for the events of the capture");
        keywords = ~static_cast<unsigned long long>(0);
    }

    const auto enabled = ::EnableTraceEx2(state_->session, &kKernelFileGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                          TRACE_LEVEL_VERBOSE, keywords, 0, 0, nullptr);
    if (enabled != ERROR_SUCCESS)
    {
        error = "the file provider of the kernel could not be enabled (error " +
                std::to_string(static_cast<unsigned long>(enabled)) + ")";
        return false;
    }

    /* The events the capture reads are the ones the machine names: the
     * identifier of an event belongs to the version of the manifest of the
     * machine, so the capture asks the machine for the identifier of every
     * event it reads by the name the manifest gives it. */
    state_->resolved_ids = ReadFileEventIds(events);
    state_->ReportFileEventNames();

    /*
     * The identifier of the version of Windows the capture was written against
     * is the fallback of a machine which does not name an event the capture
     * reads: the warning above names the events of that kind.
     */
    auto& ids = state_->file_ids;
    ids = state_->resolved_ids;
    ids.name = ids.name != 0 ? ids.name : kFileEventName;
    ids.name_rundown = ids.name_rundown != 0 ? ids.name_rundown : kFileEventNameRundown;
    ids.create = ids.create != 0 ? ids.create : kFileEventCreate;
    ids.write = ids.write != 0 ? ids.write : kFileEventWrite;
    ids.delete_path = ids.delete_path != 0 ? ids.delete_path : kFileEventDeletePath;
    ids.rename_path = ids.rename_path != 0 ? ids.rename_path : kFileEventRenamePath;
    ids.set_link_path = ids.set_link_path != 0 ? ids.set_link_path : kFileEventSetLinkPath;
    ids.create_new_file = ids.create_new_file != 0 ? ids.create_new_file : kFileEventCreateNewFile;

    /* The table and the parameters are what the report of the recording names,
     * so a reader sees what the machine declares and what it was asked for. */
    state_->provider_events = std::move(events);
    state_->provider_enable = enable;
    state_->provider_enable.keywords = keywords;

    return true;
}

void RealtimeCaptureRecorder::GiveUpState()
{
    if (state_ == nullptr)
    {
        return;
    }

    /*
     * The session is stopped and the consumer is waited for without a timeout:
     * a stopped session makes ProcessTrace() return, and a state which is
     * dropped while its consumer still runs would be a use after free.
     */
    RequestStop();
    if (state_->consumer.joinable())
    {
        state_->consumer.join();
    }

    state_.reset();
}

void RealtimeCaptureRecorder::RequestStop()
{
    if (state_ == nullptr || state_->stop_requested.exchange(true))
    {
        return;
    }

    auto*      properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(state_->properties.data());
    const auto status = ::ControlTraceW(state_->session, nullptr, properties, EVENT_TRACE_CONTROL_STOP);
    if (status != ERROR_SUCCESS)
    {
        spdlog::warn("the realtime capture could not stop its session: {}", static_cast<unsigned long>(status));
        return;
    }

    /*
     * The counters of the session are the ones it held when it was stopped and
     * the session is gone from here on, so they are taken right away. The
     * events the kernel dropped are changes the recording never saw, which is
     * what the caller reports instead of leaving the loss without a trace.
     */
    SessionStatistics statistics;
    statistics.available = true;
    statistics.events_lost = properties->EventsLost;
    statistics.real_time_buffers_lost = properties->RealTimeBuffersLost;
    statistics.log_buffers_lost = properties->LogBuffersLost;
    statistics.buffers_written = properties->BuffersWritten;
    statistics.number_of_buffers = properties->NumberOfBuffers;
    statistics.free_buffers = properties->FreeBuffers;

    {
        const std::lock_guard<std::mutex> lock(state_->statistics_mutex);
        state_->statistics = statistics;
    }

    if (statistics.events_lost > 0 || statistics.real_time_buffers_lost > 0 || statistics.log_buffers_lost > 0)
    {
        spdlog::warn("the realtime capture lost {} events and {} buffers of the session before its consumer read them",
                     statistics.events_lost, statistics.real_time_buffers_lost + statistics.log_buffers_lost);
    }
}

bool RealtimeCaptureRecorder::Join(unsigned milliseconds)
{
    if (state_ == nullptr)
    {
        return true;
    }

    std::unique_lock<std::mutex> lock(state_->finish_mutex);
    const bool finished = state_->finish_condition.wait_for(lock, std::chrono::milliseconds(milliseconds),
                                                            [this]() { return state_->finished.load(); });
    lock.unlock();

    if (!finished)
    {
        return false;
    }

    if (state_->consumer.joinable())
    {
        state_->consumer.join();
    }
    return true;
}

std::vector<RealtimeCaptureEvent> RealtimeCaptureRecorder::TakeEvents()
{
    if (state_ == nullptr)
    {
        return {};
    }

    const std::lock_guard<std::mutex> lock(state_->events_mutex);
    auto                              events = std::move(state_->events);
    state_->events.clear();
    return events;
}

RealtimeCaptureStatistics RealtimeCaptureRecorder::Statistics() const
{
    RealtimeCaptureStatistics reported;

    if (state_ == nullptr)
    {
        return reported;
    }

    SessionStatistics statistics;
    {
        const std::lock_guard<std::mutex> lock(state_->statistics_mutex);
        statistics = state_->statistics;
    }

    reported.available = statistics.available;
    reported.events_lost = statistics.events_lost;
    reported.buffers_lost = statistics.real_time_buffers_lost + statistics.log_buffers_lost;
    reported.number_of_buffers = statistics.number_of_buffers;
    reported.free_buffers = statistics.free_buffers;
    return reported;
}

bool HarvestRealtimeEvents(const std::vector<RealtimeCaptureEvent>&     events,
                           const std::vector<SnapshotEnvironmentEntry>& environment_before,
                           const RealtimeCaptureOptions& options, SnapshotDiff& diff, CaptureFillReport& report,
                           std::string& error)
{
    /*
     * The events are reduced in the order they happened, so a change which
     * was made and undone within the recording leaves no entry behind.
     */
    std::vector<const RealtimeCaptureEvent*> ordered;
    ordered.reserve(events.size());
    for (const auto& event : events)
    {
        if (!RealtimeCaptureEventExcluded(event, options))
        {
            ordered.push_back(&event);
        }
    }
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [](const RealtimeCaptureEvent* left, const RealtimeCaptureEvent* right) { return left->time < right->time; });

    std::unordered_map<std::wstring, FileEntryState>   files;
    std::unordered_map<std::wstring, ValueEntryState>  values;
    std::vector<std::pair<std::wstring, std::wstring>> deleted_keys; /* folded key and its original spelling. */

    for (const auto* entry : ordered)
    {
        switch (entry->kind)
        {
        case RealtimeCaptureEvent::Kind::FileCreated: {
            auto& file = files[snapshot::FoldText(entry->path)];
            file.path = entry->path;
            file.state = FileState::Added;
            break;
        }
        case RealtimeCaptureEvent::Kind::FileWritten: {
            auto& file = files[snapshot::FoldText(entry->path)];
            file.path = entry->path;
            if (file.state != FileState::Added)
            {
                file.state = FileState::Touched;
            }
            break;
        }
        case RealtimeCaptureEvent::Kind::FileRenamed: {
            const auto old_key = snapshot::FoldText(entry->path);
            const auto old = files.find(old_key);
            if (old != files.end() && old->second.state == FileState::Added)
            {
                /* The entry never existed before the recording. */
                files.erase(old);
            }
            else
            {
                auto& removed = files[old_key];
                removed.path = entry->path;
                removed.state = FileState::Removed;
            }

            auto& created = files[snapshot::FoldText(entry->detail)];
            created.path = entry->detail;
            created.state = FileState::Added;
            break;
        }
        case RealtimeCaptureEvent::Kind::FileDeleted: {
            const auto key = snapshot::FoldText(entry->path);
            const auto file = files.find(key);
            if (file != files.end() && file->second.state == FileState::Added)
            {
                /* The entry was created and undone within the recording. */
                files.erase(file);
            }
            else
            {
                auto& removed = files[key];
                removed.path = entry->path;
                removed.state = FileState::Removed;
            }
            break;
        }
        case RealtimeCaptureEvent::Kind::ValueSet: {
            auto& value = values[snapshot::RegistryValueLookupKey(entry->path, entry->detail)];
            value.path = entry->path;
            value.name = entry->detail;
            value.state = ValueState::Set;
            break;
        }
        case RealtimeCaptureEvent::Kind::ValueDeleted: {
            auto& value = values[snapshot::RegistryValueLookupKey(entry->path, entry->detail)];
            value.path = entry->path;
            value.name = entry->detail;
            value.state = ValueState::Deleted;
            break;
        }
        case RealtimeCaptureEvent::Kind::KeyDeleted: {
            const auto key = snapshot::FoldText(entry->path);
            const auto existing = std::find_if(deleted_keys.begin(), deleted_keys.end(),
                                               [&key](const auto& candidate) { return candidate.first == key; });
            if (existing == deleted_keys.end())
            {
                deleted_keys.emplace_back(key, entry->path);
            }
            break;
        }
        }
    }

    const auto presets = PresetDirectories();

    /* The filesystem part reads one stable order, so a run which sees the
     * same events reports the same difference. */
    std::vector<const FileEntryState*> ordered_files;
    ordered_files.reserve(files.size());
    for (const auto& item : files)
    {
        ordered_files.push_back(&item.second);
    }
    std::sort(ordered_files.begin(), ordered_files.end(),
              [](const FileEntryState* left, const FileEntryState* right) { return left->path < right->path; });

    /**
     * @brief One entry of the recording, mapped onto the view.
     */
    struct ReportedFile
    {
        SnapshotDiffFile file;                     ///< The entry of the difference.
        FileState        state = FileState::Added; ///< The change the recording reported.
    };

    /*
     * Every entry the recording reported is mapped onto the view on its own:
     * the difference carries the changes the recording saw and nothing the host
     * holds around them.
     */
    std::vector<ReportedFile> reported_files;
    for (const auto* entry : ordered_files)
    {
        std::string  preset_id;
        std::wstring relative;
        if (!MapToPreset(entry->path, presets, preset_id, relative))
        {
            /* The change belongs to no preset directory, so no workspace can carry it. */
            AddIgnoredNotice(report, CaptureFillDomain::Filesystem, UnmappedMessage(entry->path));
            continue;
        }

        /*
         * An alternate data stream (`file.txt:Zone.Identifier`) is a part of
         * the file it belongs to and not an entry of the folder view, which
         * names files and folders alone: the archive carries no stream, so the
         * recording leaves the change of one out.
         */
        if (relative.find(L':') != std::wstring::npos)
        {
            continue;
        }

        ReportedFile item;
        item.file.preset_id = preset_id;
        item.file.path = relative;
        item.state = entry->state;

        if (entry->state != FileState::Removed)
        {
            std::error_code code;
            item.file.directory = std::filesystem::is_directory(std::filesystem::path(entry->path), code) && !code;
        }

        reported_files.push_back(std::move(item));
    }

    for (auto& item : reported_files)
    {
        if (item.state == FileState::Removed)
        {
            /* A removed entry is gone, so the host cannot say whether it was
             * a folder, and the marker of the difference carries neither. */
            item.file.directory = false;
            diff.removed_files.push_back(std::move(item.file));
            continue;
        }

        if (item.state == FileState::Touched)
        {
            diff.modified_files.push_back(std::move(item.file));
            continue;
        }

        diff.added_files.push_back(std::move(item.file));
    }

    /* The registry part reads one stable order as well. */
    std::vector<const ValueEntryState*> ordered_values;
    ordered_values.reserve(values.size());
    for (const auto& item : values)
    {
        ordered_values.push_back(&item.second);
    }
    std::sort(ordered_values.begin(), ordered_values.end(),
              [](const ValueEntryState* left, const ValueEntryState* right) {
                  if (left->path != right->path)
                  {
                      return left->path < right->path;
                  }
                  return left->name < right->name;
              });

    std::vector<std::pair<std::wstring, std::wstring>> written_keys; /* folded key and its original spelling. */
    for (const auto* entry : ordered_values)
    {
        if (entry->state == ValueState::Deleted)
        {
            SnapshotDiffRegistryValue value;
            value.path = entry->path;
            value.name = entry->name;
            diff.removed_registry_values.push_back(std::move(value));
            continue;
        }

        SnapshotDiffRegistryValue value;
        value.path = entry->path;
        value.name = entry->name;
        if (!ReadRegistryValue(entry->path, entry->name, value.type, value.data))
        {
            spdlog::warn("the realtime capture could not read the registry value '{}' of '{}'", WideToUTF8(entry->name),
                         WideToUTF8(entry->path));
            continue;
        }
        diff.added_registry_values.push_back(std::move(value));

        const auto key = snapshot::FoldText(entry->path);
        const auto existing = std::find_if(written_keys.begin(), written_keys.end(),
                                           [&key](const auto& candidate) { return candidate.first == key; });
        if (existing == written_keys.end())
        {
            written_keys.emplace_back(key, entry->path);
        }
    }

    for (const auto& key : written_keys)
    {
        const auto deleted = std::find_if(deleted_keys.begin(), deleted_keys.end(),
                                          [&key](const auto& candidate) { return candidate.first == key.first; });
        if (deleted == deleted_keys.end())
        {
            diff.added_registry_keys.push_back(key.second);
        }
    }
    for (const auto& key : deleted_keys)
    {
        const auto written = std::find_if(written_keys.begin(), written_keys.end(),
                                          [&key](const auto& candidate) { return candidate.first == key.first; });
        if (written == written_keys.end())
        {
            diff.removed_registry_keys.push_back(key.second);
        }
    }

    /* The environment part is the difference of the persisted environment of
     * the machine and the state the recording began from. */
    std::vector<SnapshotEnvironmentEntry> environment_after;
    if (!ReadPersistedEnvironment(environment_after, error))
    {
        return false;
    }

    std::unordered_map<std::wstring, const SnapshotEnvironmentEntry*> before;
    for (const auto& entry : environment_before)
    {
        before[snapshot::EnvironmentLookupKey(entry.name)] = &entry;
    }

    std::unordered_set<std::wstring> remaining_before;
    for (const auto& item : before)
    {
        remaining_before.insert(item.first);
    }

    for (const auto& entry : environment_after)
    {
        SnapshotDiffEnvironment candidate;
        candidate.name = entry.name;
        candidate.value = entry.value;

        const auto name = snapshot::EnvironmentLookupKey(entry.name);
        const auto previous = before.find(name);
        if (previous == before.end())
        {
            diff.added_environment.push_back(std::move(candidate));
        }
        else if (previous->second->value != entry.value)
        {
            diff.modified_environment.push_back(std::move(candidate));
        }
        remaining_before.erase(name);
    }

    for (const auto& name : remaining_before)
    {
        SnapshotDiffEnvironment entry;
        const auto              previous = before.find(name);
        if (previous != before.end())
        {
            entry.name = previous->second->name;
            entry.value = previous->second->value;
            diff.removed_environment.push_back(std::move(entry));
        }
    }

    return true;
}

} // namespace appbox
