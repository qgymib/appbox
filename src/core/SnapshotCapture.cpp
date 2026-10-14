#include "SnapshotCapture.hpp"
#include "PresetDirectory.hpp"
#include "RegistryModel.hpp"
#include "WString.hpp"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Environment key of the machine.
 *
 * The key is the persisted environment of the machine, which the operating
 * system hands to a process it starts; the environment of the packer itself is
 * a copy of it which never changes while the packer runs.
 */
constexpr const wchar_t* kMachineEnvironmentKey = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

/**
 * @brief Environment key of the current user.
 */
constexpr const wchar_t* kUserEnvironmentKey = L"Environment";

/**
 * @brief Depth at which the registry walk stops.
 *
 * The kernel refuses to create a key deeper than 512 levels, so a tree which
 * is deeper than this is broken and must not drive the walk into a stack
 * overflow.
 */
constexpr int kMaxRegistryDepth = 512;

/* Size the registry enumeration starts with. */
constexpr DWORD kInitialNameChars = 256;
constexpr DWORD kInitialDataBytes = 1024;

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

    const auto count = static_cast<int>(left.size());
    return ::CompareStringOrdinal(left.data(), count, right.data(), count, TRUE) == CSTR_EQUAL;
}

/**
 * @brief Whether a path is a folder below another one.
 *
 * The ancestor has to be a whole component prefix, so `C:\Program FilesX` is
 * not below `C:\Program Files`.
 *
 * @param[in] path Path to test.
 * @param[in] ancestor Folder to test against.
 * @return true when the path is a strict descendant of the ancestor.
 */
bool IsPathBelow(const std::wstring& path, const std::wstring& ancestor)
{
    if (ancestor.empty() || path.size() <= ancestor.size())
    {
        return false;
    }
    if (!EqualsIgnoreCase(path.substr(0, ancestor.size()), ancestor))
    {
        return false;
    }

    return path[ancestor.size()] == L'\\';
}

/**
 * @brief Whether a host path is one the capture must not record.
 *
 * The comparison folds the case, because the filesystem of Windows compares
 * the names of its entries without regard to the case.
 *
 * @param[in] excluded Host files the capture must not record.
 * @param[in] path Host path of the entry which is being read.
 * @return true when the entry has to be left out.
 */
bool IsExcluded(const std::vector<std::wstring>& excluded, const std::wstring& path)
{
    for (const auto& candidate : excluded)
    {
        if (candidate.size() != path.size())
        {
            continue;
        }

        if (::CompareStringOrdinal(path.data(), static_cast<int>(path.size()), candidate.data(),
                                   static_cast<int>(candidate.size()), TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Normalize the host paths a capture leaves out.
 *
 * @param[in] paths The paths to normalize.
 * @return The normalized paths, without the empty ones.
 */
std::vector<std::wstring> NormalizeExclusions(const std::vector<std::wstring>& paths)
{
    std::vector<std::wstring> normalized;
    for (const auto& path : paths)
    {
        if (path.empty())
        {
            continue;
        }

        normalized.push_back(std::filesystem::path(path).lexically_normal().wstring());
    }

    return normalized;
}

/**
 * @brief Build the path of a folder as a Win32 path.
 *
 * A path which is long enough to leave the limit of `MAX_PATH` is prefixed
 * with the object namespace, which is what lets the walk read a deep folder of
 * a preset directory as well.
 *
 * @param[in] path Host folder.
 * @return The path to hand to the Win32 API.
 */
std::wstring ExtendedPath(const std::wstring& path)
{
    if (path.size() < MAX_PATH || path.compare(0, 4, L"\\\\?\\") == 0)
    {
        return path;
    }
    if (path.size() < 2 || path[1] != L':')
    {
        /* Only a drive path can be prefixed; a UNC path would need another form. */
        return path;
    }

    return L"\\\\?\\" + path;
}

/**
 * @brief One entry of a folder of the host.
 */
struct FolderEntry
{
    std::wstring  name; ///< Name of the entry inside its folder.
    DWORD         attributes = 0;
    std::uint64_t size = 0;       ///< Size in bytes, zero for a folder.
    std::int64_t  write_time = 0; ///< Last write time, 100ns units since 1601 UTC.
};

/**
 * @brief One value of a registry key.
 */
struct ValueEntry
{
    std::wstring              name; ///< Name of the value, empty for the default value.
    DWORD                     type = 0;
    std::vector<std::uint8_t> data; ///< Raw data of the value.
};

/**
 * @brief Owns the handle of an open registry key.
 *
 * Only a key the walk opened itself is closed: the five predefined root key
 * handles belong to the process and are never released.
 */
struct KeyHandle
{
    HKEY handle = nullptr;

    ~KeyHandle()
    {
        if (handle != nullptr)
        {
            ::RegCloseKey(handle);
        }
    }

    KeyHandle() = default;
    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;
};

/**
 * @brief The state one capture keeps while it walks through the machine.
 */
struct CaptureContext
{
    appbox::SnapshotWriter*                writer = nullptr;   ///< Phase which receives the entries.
    const appbox::CaptureProgressCallback* progress = nullptr; ///< Progress callback, may be null.
    appbox::CaptureStage                   stage = appbox::CaptureStage::Filesystem;
    std::vector<std::wstring>              excluded;          ///< Host files the walk must not record.
    std::size_t                            done = 0;          ///< Entries recorded so far.
    std::size_t                            skipped = 0;       ///< Folders and keys which could not be read.
    bool                                   cancelled = false; ///< Whether the callback aborted the capture.
    std::string                            write_error;       ///< Error of a failed write, empty while none failed.

    /**
     * @brief Report the entry which is being read.
     * @param[in] current The folder, key or variable being read.
     * @return false when the callback aborted the capture.
     */
    bool Report(const std::wstring& current)
    {
        if (progress == nullptr || !*progress)
        {
            return true;
        }

        if (!(*progress)(appbox::CaptureProgress{ stage, done, skipped, current }))
        {
            cancelled = true;
            return false;
        }

        return true;
    }

    /**
     * @brief Write one entry of the filesystem part of the phase.
     * @param[in] entry The entry to write.
     * @return false when the write failed, see write_error.
     */
    bool WriteFile(const appbox::SnapshotFileEntry& entry)
    {
        return writer->WriteFile(entry, write_error);
    }

    /**
     * @brief Write one key of the registry part of the phase.
     * @param[in] path Path of the key.
     * @return false when the write failed, see write_error.
     */
    bool WriteRegistryKey(const std::wstring& path)
    {
        return writer->WriteRegistryKey(path, write_error);
    }

    /**
     * @brief Write one value of the registry part of the phase.
     * @param[in] entry The value to write.
     * @return false when the write failed, see write_error.
     */
    bool WriteRegistryValue(const appbox::SnapshotRegistryValueEntry& entry)
    {
        return writer->WriteRegistryValue(entry, write_error);
    }

    /**
     * @brief Write one variable of the environment part of the phase.
     * @param[in] entry The variable to write.
     * @return false when the write failed, see write_error.
     */
    bool WriteEnvironment(const appbox::SnapshotEnvironmentEntry& entry)
    {
        return writer->WriteEnvironment(entry, write_error);
    }
};

/**
 * @brief Read one folder of the host.
 *
 * @param[in] directory Folder to read.
 * @param[out] entries The entries of the folder, empty when it holds none.
 * @param[out] error Error description when the folder cannot be read.
 * @return true when the folder was read.
 */
bool EnumerateFolder(const std::wstring& directory, std::vector<FolderEntry>& entries, std::string& error)
{
    entries.clear();

    WIN32_FIND_DATAW data = {};
    const auto       handle = ::FindFirstFileExW((ExtendedPath(directory) + L"\\*").c_str(), FindExInfoBasic, &data,
                                                 FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const auto status = ::GetLastError();
        if (status == ERROR_FILE_NOT_FOUND)
        {
            /* A folder without an entry reports this status instead of a handle. */
            return true;
        }

        error = "failed to read '" + appbox::WideToUTF8(directory) + "' (error " + std::to_string(status) + ")";
        return false;
    }

    for (;;)
    {
        const std::wstring name = data.cFileName;
        if (name != L"." && name != L"..")
        {
            FolderEntry entry;
            entry.name = name;
            entry.attributes = data.dwFileAttributes;
            entry.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            entry.write_time = (static_cast<std::int64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                               data.ftLastWriteTime.dwLowDateTime;
            entries.push_back(std::move(entry));
        }

        if (::FindNextFileW(handle, &data) != FALSE)
        {
            continue;
        }

        const auto status = ::GetLastError();
        ::FindClose(handle);
        if (status != ERROR_NO_MORE_FILES)
        {
            error = "failed to read '" + appbox::WideToUTF8(directory) + "' (error " + std::to_string(status) + ")";
            return false;
        }

        return true;
    }
}

/**
 * @brief Read every value of a registry key.
 *
 * A value whose name or data does not fit the buffer is read again with the
 * size the enumeration reported, and a value which cannot be read at all is
 * skipped, so one broken value does not hide the rest of the key.
 *
 * @param[in] key Key to read.
 * @param[out] values The values of the key, in enumeration order.
 * @return true when the key was read.
 */
bool ReadValues(HKEY key, std::vector<ValueEntry>& values)
{
    values.clear();

    for (DWORD index = 0;; ++index)
    {
        std::wstring              name(kInitialNameChars, L'\0');
        std::vector<std::uint8_t> data(kInitialDataBytes);
        DWORD                     name_chars = static_cast<DWORD>(name.size());
        DWORD                     data_bytes = static_cast<DWORD>(data.size());
        DWORD                     type = 0;

        auto status = ::RegEnumValueW(key, index, name.data(), &name_chars, nullptr, &type, data.data(), &data_bytes);
        if (status == ERROR_MORE_DATA)
        {
            name.resize(name_chars);
            data.resize(data_bytes);
            name_chars = static_cast<DWORD>(name.size());
            data_bytes = static_cast<DWORD>(data.size());
            status = ::RegEnumValueW(key, index, name.data(), &name_chars, nullptr, &type, data.data(), &data_bytes);
        }

        if (status == ERROR_NO_MORE_ITEMS)
        {
            return true;
        }
        if (status != ERROR_SUCCESS)
        {
            /* The value is skipped; the enumeration continues with the next one. */
            continue;
        }

        ValueEntry value;
        value.name.assign(name.data(), name_chars);
        value.type = type;
        value.data.assign(data.begin(), data.begin() + data_bytes);
        values.push_back(std::move(value));
    }
}

/**
 * @brief Read the text of a registry value of a string type.
 * @param[in] value The value to read.
 * @param[out] text The text without its terminators.
 * @return true when the value holds a string.
 */
bool ValueText(const ValueEntry& value, std::wstring& text)
{
    if (value.type != REG_SZ && value.type != REG_EXPAND_SZ)
    {
        return false;
    }
    if (value.data.size() % sizeof(wchar_t) != 0)
    {
        return false;
    }

    text.clear();
    if (value.data.empty())
    {
        return true;
    }

    text.assign(reinterpret_cast<const wchar_t*>(value.data.data()), value.data.size() / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0')
    {
        text.pop_back();
    }
    return true;
}

/**
 * @brief Resolve the `%NAME%` references of a text.
 *
 * The references are resolved against the environment of the process, which is
 * the environment the machine had when the packer started; a value of the
 * persisted environment therefore becomes the text the packaged application
 * would read from it.
 *
 * @param[in] text Text to expand.
 * @return The expanded text, the input when it cannot be expanded.
 */
std::wstring ExpandText(const std::wstring& text)
{
    if (text.empty())
    {
        return text;
    }

    std::wstring buffer(256, L'\0');
    for (;;)
    {
        const auto length = ::ExpandEnvironmentStringsW(text.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return text;
        }
        if (length <= buffer.size())
        {
            /* The count includes the terminator, which the text does not carry. */
            buffer.resize(static_cast<std::size_t>(length) - 1);
            return buffer;
        }

        buffer.resize(length);
    }
}

/**
 * @brief Resolve the folder a filesystem root names.
 *
 * @param[in] root The root to resolve.
 * @param[out] preset The preset directory the root belongs to.
 * @param[out] prefix Path of the scanned folder relative to the preset
 *             directory, empty when the root is the preset directory itself.
 * @param[out] error Error description on failure.
 * @return true when the root is usable.
 */
bool ResolveRoot(const appbox::CaptureFilesystemRoot& root, appbox::PresetDirectory& preset, std::wstring& prefix,
                 std::string& error)
{
    if (!appbox::FindPresetDirectory(root.preset_id, preset))
    {
        error = "unknown preset directory: " + root.preset_id;
        return false;
    }
    if (root.scan_path.empty())
    {
        error = "the folder of the preset directory is empty";
        return false;
    }

    const auto scan = std::filesystem::path(root.scan_path).lexically_normal().wstring();
    const auto real = std::filesystem::path(preset.real_path).lexically_normal().wstring();

    prefix.clear();
    if (EqualsIgnoreCase(scan, real))
    {
        return true;
    }
    if (!IsPathBelow(scan, real))
    {
        error = "the folder is not part of the preset directory: " + appbox::WideToUTF8(root.scan_path);
        return false;
    }

    prefix = scan.substr(real.size());
    while (!prefix.empty() && prefix.front() == L'\\')
    {
        prefix.erase(prefix.begin());
    }
    return true;
}

/**
 * @brief Walk through one filesystem root.
 *
 * A folder which cannot be read is skipped and counted, so a capture of a
 * whole machine reports the parts it is missing instead of failing.
 *
 * @param[in] root The root to walk through.
 * @param[in,out] context The state of the capture.
 * @param[out] error Error description on failure.
 * @return false when the root itself is not usable.
 */
bool WalkFilesystemRoot(const appbox::CaptureFilesystemRoot& root, CaptureContext& context, std::string& error)
{
    appbox::PresetDirectory preset;
    std::wstring            prefix;
    if (!ResolveRoot(root, preset, prefix, error))
    {
        return false;
    }

    const auto scan = std::filesystem::path(root.scan_path).lexically_normal().wstring();

    /* The scanned folder itself is an entry of the view when it hangs below the preset. */
    if (!prefix.empty())
    {
        appbox::SnapshotFileEntry entry;
        entry.preset_id = root.preset_id;
        entry.path = prefix;
        entry.directory = true;
        if (!context.WriteFile(entry))
        {
            return false;
        }
        ++context.done;
    }

    struct PendingFolder
    {
        std::wstring path;     ///< Host folder to read.
        std::wstring relative; ///< Path of the folder relative to the preset directory.
    };

    std::vector<PendingFolder> pending;
    pending.push_back(PendingFolder{ scan, prefix });

    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();

        if (!context.Report(current.path))
        {
            return true;
        }

        std::vector<FolderEntry> entries;
        std::string              read_error;
        if (!EnumerateFolder(current.path, entries, read_error))
        {
            ++context.skipped;
            spdlog::warn("the capture skipped a folder: {}", read_error);
            continue;
        }

        for (const auto& entry : entries)
        {
            /*
             * A reparse point is not followed: the folder it names is part of
             * another place of the view, and walking into it would record the
             * same content twice (or run in a circle).
             */
            if ((entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                continue;
            }

            const bool directory = (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool file = (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            if (!directory && !file)
            {
                continue;
            }

            const auto host_path = current.path + L"\\" + entry.name;

            /*
             * The file a capture writes may live below the folder which is
             * being read, so it is left out: a snapshot never describes
             * itself.
             */
            if (IsExcluded(context.excluded, host_path))
            {
                continue;
            }

            const auto relative = current.relative.empty() ? entry.name : current.relative + L"\\" + entry.name;

            appbox::SnapshotFileEntry recorded;
            recorded.preset_id = root.preset_id;
            recorded.path = relative;
            recorded.directory = directory;
            recorded.size = directory ? 0 : entry.size;
            recorded.write_time = entry.write_time;
            if (!context.WriteFile(recorded))
            {
                return false;
            }
            ++context.done;

            if (directory)
            {
                pending.push_back(PendingFolder{ host_path, relative });
            }
        }
    }

    return true;
}

/**
 * @brief Walk through one registry key and everything below it.
 *
 * @param[in] key Handle of the key to read.
 * @param[in] path Path of the key, which is the path the model uses.
 * @param[in,out] context The state of the capture.
 * @param[in] depth Depth of the key below its root key.
 * @return false when the progress callback aborted the capture.
 */
bool WalkRegistryKey(HKEY key, const std::wstring& path, CaptureContext& context, int depth)
{
    if (!context.Report(path))
    {
        return false;
    }

    if (!context.WriteRegistryKey(path))
    {
        return false;
    }
    ++context.done;

    std::vector<ValueEntry> values;
    if (ReadValues(key, values))
    {
        for (const auto& value : values)
        {
            appbox::SnapshotRegistryValueEntry entry;
            entry.path = path;
            entry.name = value.name;
            entry.type = value.type;
            entry.data = value.data;
            if (!context.WriteRegistryValue(entry))
            {
                return false;
            }
            ++context.done;
        }
    }
    else
    {
        ++context.skipped;
    }

    if (depth >= kMaxRegistryDepth)
    {
        spdlog::warn("the capture stopped at a registry key which is too deep: {}", appbox::WideToUTF8(path));
        return true;
    }

    for (DWORD index = 0;; ++index)
    {
        std::wstring name(kInitialNameChars, L'\0');
        DWORD        name_chars = static_cast<DWORD>(name.size());

        auto status = ::RegEnumKeyExW(key, index, name.data(), &name_chars, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_MORE_DATA)
        {
            name.resize(name_chars);
            name_chars = static_cast<DWORD>(name.size());
            status = ::RegEnumKeyExW(key, index, name.data(), &name_chars, nullptr, nullptr, nullptr, nullptr);
        }

        if (status == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (status != ERROR_SUCCESS)
        {
            ++context.skipped;
            spdlog::warn("the capture skipped a registry key below {} (error {})", appbox::WideToUTF8(path),
                         static_cast<int>(status));
            break;
        }

        name.resize(name_chars);

        KeyHandle child;
        if (::RegOpenKeyExW(key, name.c_str(), 0, KEY_READ, &child.handle) != ERROR_SUCCESS)
        {
            ++context.skipped;
            continue;
        }

        if (!WalkRegistryKey(child.handle, path + L"\\" + name, context, depth + 1))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Walk through the registry roots of a capture.
 * @param[in] roots Names of the root keys to walk through.
 * @param[in,out] context The state of the capture.
 * @param[out] error Error description on failure.
 * @return false when a root is not usable.
 */
bool WalkRegistry(const std::vector<std::wstring>& roots, CaptureContext& context, std::string& error)
{
    /*
     * The five predefined handles of the process, which the walk never closes:
     * a key the walk opened itself is released by KeyHandle instead.
     */
    struct RootKey
    {
        const wchar_t* name;
        HKEY           handle;
    };

    static const RootKey kRootKeys[] = {
        { L"HKEY_CLASSES_ROOT",   HKEY_CLASSES_ROOT   },
        { L"HKEY_CURRENT_USER",   HKEY_CURRENT_USER   },
        { L"HKEY_LOCAL_MACHINE",  HKEY_LOCAL_MACHINE  },
        { L"HKEY_USERS",          HKEY_USERS          },
        { L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
    };

    for (const auto& root : roots)
    {
        std::wstring canonical;
        HKEY         handle = nullptr;

        for (const auto& name : appbox::RegistryRootKeyNames())
        {
            if (EqualsIgnoreCase(name, root))
            {
                canonical = name;
                break;
            }
        }
        if (canonical.empty())
        {
            error = "unknown registry root key: " + appbox::WideToUTF8(root);
            return false;
        }

        for (const auto& candidate : kRootKeys)
        {
            if (canonical == candidate.name)
            {
                handle = candidate.handle;
                break;
            }
        }
        if (handle == nullptr)
        {
            error = "the registry root key is not available: " + appbox::WideToUTF8(root);
            return false;
        }

        if (!WalkRegistryKey(handle, canonical, context, 0))
        {
            return true;
        }
    }

    return true;
}

/**
 * @brief Read the persisted environment of the machine.
 *
 * The variables of the machine are read first and the variables of the current
 * user follow, so a name both keys list keeps the value of the user, which is
 * the value the operating system hands to a process of that user. The writer
 * applies that precedence on its own: an entry of the environment replaces the
 * entry of the same name, which is why the run which reads the user key writes
 * last.
 *
 * @param[in,out] context The state of the capture.
 * @return false when the progress callback aborted the capture.
 */
bool ReadEnvironment(CaptureContext& context)
{
    struct EnvironmentKey
    {
        HKEY           root;
        const wchar_t* sub_key;
    };

    const EnvironmentKey keys[] = {
        { HKEY_LOCAL_MACHINE, kMachineEnvironmentKey },
        { HKEY_CURRENT_USER,  kUserEnvironmentKey    },
    };

    for (const auto& source : keys)
    {
        KeyHandle key;
        if (::RegOpenKeyExW(source.root, source.sub_key, 0, KEY_READ, &key.handle) != ERROR_SUCCESS)
        {
            ++context.skipped;
            continue;
        }

        std::vector<ValueEntry> values;
        if (!ReadValues(key.handle, values))
        {
            ++context.skipped;
            continue;
        }

        for (const auto& value : values)
        {
            if (value.name.empty())
            {
                continue;
            }
            if (!context.Report(value.name))
            {
                return false;
            }

            std::wstring text;
            if (!ValueText(value, text))
            {
                ++context.skipped;
                continue;
            }

            appbox::SnapshotEnvironmentEntry entry;
            entry.name = value.name;
            entry.value = ExpandText(text);

            if (!context.WriteEnvironment(entry))
            {
                return false;
            }
            ++context.done;
        }
    }

    return true;
}

} // namespace

namespace appbox
{

std::vector<CaptureFilesystemRoot> DefaultCaptureFilesystemRoots()
{
    std::vector<CaptureFilesystemRoot> roots;
    for (const auto& preset : PresetDirectories())
    {
        CaptureFilesystemRoot root;
        root.preset_id = preset.id;
        root.scan_path = preset.real_path;
        roots.push_back(std::move(root));
    }

    return roots;
}

std::vector<std::wstring> DefaultCaptureRegistryRoots()
{
    return RegistryRootKeyNames();
}

bool CaptureSnapshot(const CaptureOptions& options, SnapshotWriter& writer, const CaptureProgressCallback& progress,
                     std::string& error)
{
    CaptureContext context;
    context.writer = &writer;
    context.progress = &progress;
    context.excluded = NormalizeExclusions(options.excluded_paths);

    /*
     * A write which failed aborts the capture. The helpers which read a folder
     * or a key report a root which is not usable through the error of their
     * own, so the write is the case which has to be told apart.
     */
    const auto write_failed = [&context](std::string& description) {
        if (context.write_error.empty())
        {
            return false;
        }

        description = context.write_error;
        return true;
    };

    context.stage = CaptureStage::Filesystem;
    for (const auto& root : options.filesystem_roots)
    {
        if (!WalkFilesystemRoot(root, context, error))
        {
            write_failed(error);
            return false;
        }
        if (context.cancelled)
        {
            error = kCaptureCancelledError;
            return false;
        }
    }

    context.stage = CaptureStage::Registry;
    if (!WalkRegistry(options.registry_roots, context, error))
    {
        write_failed(error);
        return false;
    }
    if (context.cancelled)
    {
        error = kCaptureCancelledError;
        return false;
    }

    context.stage = CaptureStage::Environment;
    if (!ReadEnvironment(context))
    {
        if (!write_failed(error))
        {
            error = kCaptureCancelledError;
        }
        return false;
    }
    if (context.cancelled)
    {
        error = kCaptureCancelledError;
        return false;
    }

    if (context.skipped > 0)
    {
        spdlog::warn("the capture skipped {} folders or keys it could not read", context.skipped);
    }

    return true;
}

} // namespace appbox
