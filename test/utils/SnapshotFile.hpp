#ifndef APPBOX_TEST_UTILS_SNAPSHOT_FILE_HPP
#define APPBOX_TEST_UTILS_SNAPSHOT_FILE_HPP

#include "src/core/SnapshotStore.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace appbox::test
{

/**
 * @brief The entries of one phase of a snapshot file a test builds.
 *
 * A case which compares two states describes them with this structure and
 * writes them into one file, which is the shape the comparison reads: the two
 * phases of a snapshot file and not two files of their own.
 */
struct SnapshotPhaseContent
{
    /**
     * @brief The entries of the filesystem part.
     */
    std::vector<appbox::SnapshotFileEntry> files;

    /**
     * @brief The paths of the registry keys.
     */
    std::vector<std::wstring> registry_keys;

    /**
     * @brief The values of the registry part.
     */
    std::vector<appbox::SnapshotRegistryValueEntry> registry_values;

    /**
     * @brief The variables of the environment part.
     */
    std::vector<appbox::SnapshotEnvironmentEntry> environment;
};

/**
 * @brief Write one phase of a snapshot file, creating the file when it is absent.
 *
 * @param[in] path Path of the snapshot file.
 * @param[in] phase Phase to write.
 * @param[in] content The entries of the phase.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
inline bool WriteSnapshotPhase(const std::wstring& path, appbox::SnapshotPhase phase,
                               const SnapshotPhaseContent& content, std::string& error)
{
    std::error_code ec;
    const bool      exists = std::filesystem::exists(std::filesystem::path(path), ec) && !ec;

    const auto writer =
        exists ? appbox::SnapshotWriter::Open(path, error) : appbox::SnapshotWriter::Create(path, error);
    if (writer == nullptr)
    {
        return false;
    }

    if (!writer->BeginPhase(phase, error))
    {
        return false;
    }

    for (const auto& entry : content.files)
    {
        if (!writer->WriteFile(entry, error))
        {
            return false;
        }
    }
    for (const auto& key : content.registry_keys)
    {
        if (!writer->WriteRegistryKey(key, error))
        {
            return false;
        }
    }
    for (const auto& value : content.registry_values)
    {
        if (!writer->WriteRegistryValue(value, error))
        {
            return false;
        }
    }
    for (const auto& variable : content.environment)
    {
        if (!writer->WriteEnvironment(variable, error))
        {
            return false;
        }
    }

    return writer->CommitPhase(error);
}

/**
 * @brief Write both phases of a snapshot file.
 * @param[in] path Path of the snapshot file, which is created or replaced.
 * @param[in] before The state before the change.
 * @param[in] after The state after the change.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
inline bool WriteSnapshotFile(const std::wstring& path, const SnapshotPhaseContent& before,
                              const SnapshotPhaseContent& after, std::string& error)
{
    return WriteSnapshotPhase(path, appbox::SnapshotPhase::Before, before, error) &&
           WriteSnapshotPhase(path, appbox::SnapshotPhase::After, after, error);
}

/**
 * @brief Read the entries of one phase of a snapshot file.
 * @param[in] path Path of the snapshot file.
 * @param[in] phase Phase to read.
 * @param[out] content The entries of the phase.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
inline bool ReadSnapshotPhase(const std::wstring& path, appbox::SnapshotPhase phase, SnapshotPhaseContent& content,
                              std::string& error)
{
    const auto reader = appbox::SnapshotReader::Open(path, error);
    if (reader == nullptr)
    {
        return false;
    }

    SnapshotPhaseContent result;
    const auto           keep_file = [&result](const appbox::SnapshotFileEntry& entry) {
        result.files.push_back(entry);
        return true;
    };
    const auto keep_key = [&result](const std::wstring& path) {
        result.registry_keys.push_back(path);
        return true;
    };
    const auto keep_value = [&result](const appbox::SnapshotRegistryValueEntry& entry) {
        result.registry_values.push_back(entry);
        return true;
    };
    const auto keep_variable = [&result](const appbox::SnapshotEnvironmentEntry& entry) {
        result.environment.push_back(entry);
        return true;
    };

    if (!reader->ForEachFile(phase, keep_file, error) || !reader->ForEachRegistryKey(phase, keep_key, error) ||
        !reader->ForEachRegistryValue(phase, keep_value, error) ||
        !reader->ForEachEnvironment(phase, keep_variable, error))
    {
        return false;
    }

    content = std::move(result);
    return true;
}

/**
 * @brief Get the entry of a path of the filesystem part of a phase.
 * @param[in] content The entries of the phase.
 * @param[in] path Path below the preset directory.
 * @return The entry, null when the phase does not hold the path.
 */
inline const appbox::SnapshotFileEntry* FindFileEntry(const SnapshotPhaseContent& content, const std::wstring& path)
{
    for (const auto& entry : content.files)
    {
        if (entry.path == path)
        {
            return &entry;
        }
    }

    return nullptr;
}

/**
 * @brief Get the value of a key and a name of the registry part of a phase.
 * @param[in] content The entries of the phase.
 * @param[in] path Path of the key which holds the value.
 * @param[in] name Name of the value.
 * @return The value, null when the phase does not hold it.
 */
inline const appbox::SnapshotRegistryValueEntry* FindRegistryValueEntry(const SnapshotPhaseContent& content,
                                                                        const std::wstring&         path,
                                                                        const std::wstring&         name)
{
    for (const auto& entry : content.registry_values)
    {
        if (entry.path == path && entry.name == name)
        {
            return &entry;
        }
    }

    return nullptr;
}

/**
 * @brief A snapshot file inside the temporary directory of the machine.
 *
 * The file is removed again when the helper goes out of scope, so a case which
 * writes a snapshot file leaves nothing behind.
 */
class TempSnapshotFile
{
public:
    /**
     * @brief Build the path of a snapshot file which does not exist yet.
     * @param[in] name Name of the file, which keeps the cases apart.
     */
    explicit TempSnapshotFile(const std::wstring& name) : path_(BuildPath(name))
    {
    }

    ~TempSnapshotFile()
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path_), ec);
    }

    TempSnapshotFile(const TempSnapshotFile&) = delete;
    TempSnapshotFile& operator=(const TempSnapshotFile&) = delete;
    TempSnapshotFile(TempSnapshotFile&&) = delete;
    TempSnapshotFile& operator=(TempSnapshotFile&&) = delete;

    /**
     * @brief Get the path of the file.
     * @return The path.
     */
    const std::wstring& Get() const
    {
        return path_;
    }

private:
    /**
     * @brief Build a unique path below the temporary directory.
     * @param[in] name Name of the file.
     * @return The path.
     */
    static std::wstring BuildPath(const std::wstring& name)
    {
        static unsigned counter = 0;

        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto unique = std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
        return (std::filesystem::temp_directory_path() / (L"appbox-snapshot-" + unique + L"-" + name)).wstring();
    }

    /**
     * @brief Path of the file.
     */
    std::wstring path_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_SNAPSHOT_FILE_HPP
