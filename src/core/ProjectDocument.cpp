#include "ProjectDocument.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>
#include <utility>

namespace
{

/**
 * @brief The JSON type of a project file document.
 *
 * The ordered type keeps the members in the order they were written, so the
 * text of a document is stable and easy to read and diff.
 */
using Json = nlohmann::ordered_json;

/* Member names of the document schema. */
constexpr const char* kVersionKey = "version";
constexpr const char* kOutputPathKey = "output_path";
constexpr const char* kFoldersKey = "folders";
constexpr const char* kFilesKey = "files";
constexpr const char* kMainProgramKey = "main_program";
constexpr const char* kPresetKey = "preset";
constexpr const char* kNameKey = "name";
constexpr const char* kSourceKey = "source";
constexpr const char* kTargetDirKey = "target_dir";
constexpr const char* kFolderKey = "folder";
constexpr const char* kPathKey = "path";

/* Member names of the registry part of the schema. */
constexpr const char* kRegistryKey = "registry";
constexpr const char* kChildrenKey = "children";
constexpr const char* kValuesKey = "values";
constexpr const char* kTypeKey = "type";
constexpr const char* kDataKey = "data";
constexpr const char* kIsolationKey = "isolation";

/* Member names of the filesystem part of the schema. */
constexpr const char* kFilesystemKey = "filesystem";
constexpr const char* kKindKey = "kind";

/**
 * @brief Reject a JSON value which is not an object.
 * @param[in] json The value to check.
 * @throw appbox::ProjectDocumentError The value is not an object.
 */
void RequireObject(const Json& json)
{
    if (!json.is_object())
    {
        throw appbox::ProjectDocumentError("the entry is not a JSON object");
    }
}

/**
 * @brief Read a required member which holds a string.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The text of the member.
 * @throw appbox::ProjectDocumentError The member is missing or not a string.
 */
std::string ReadRequiredString(const Json& json, const char* key)
{
    const auto member = json.find(key);
    if (member == json.end() || !member->is_string())
    {
        throw appbox::ProjectDocumentError(std::string("the '") + key + "' member is missing or not a string");
    }

    return member->get<std::string>();
}

/**
 * @brief Read a required member which holds a string as wide text.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The text of the member.
 * @throw appbox::ProjectDocumentError The member is missing or not a string.
 */
std::wstring ReadRequiredText(const Json& json, const char* key)
{
    return appbox::UTF8ToWide(ReadRequiredString(json, key));
}

/**
 * @brief Read a member which holds a string, empty when it is absent.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The text of the member, empty when the member is absent.
 * @throw appbox::ProjectDocumentError The member is present but not a string.
 */
std::wstring ReadOptionalText(const Json& json, const char* key)
{
    const auto member = json.find(key);
    if (member == json.end())
    {
        return {};
    }
    if (!member->is_string())
    {
        throw appbox::ProjectDocumentError(std::string("the '") + key + "' member is not a string");
    }

    return appbox::UTF8ToWide(member->get<std::string>());
}

/**
 * @brief Read the isolation mode of a registry entry.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The isolation mode of the member.
 * @throw appbox::ProjectDocumentError The mode is unknown or not a string.
 */
appbox::RegistryIsolation ReadRegistryIsolation(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::RegistryIsolation isolation = appbox::RegistryIsolation::WriteCopy;
    if (!appbox::registry_isolation::ParseIsolationToken(text, isolation))
    {
        throw appbox::ProjectDocumentError("unknown isolation mode '" + text + "'");
    }

    return isolation;
}

/**
 * @brief Read the type of a registry value.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The type of the member.
 * @throw appbox::ProjectDocumentError The type is unknown or not a string.
 */
appbox::RegistryValueType ReadRegistryValueType(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::RegistryValueType type = appbox::RegistryValueType::String;
    if (!appbox::ParseRegistryValueType(appbox::UTF8ToWide(text), type))
    {
        throw appbox::ProjectDocumentError("unknown value type '" + text + "'");
    }

    return type;
}

/**
 * @brief Read the raw data of a registry value from its hexadecimal text.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The raw bytes of the member.
 * @throw appbox::ProjectDocumentError The data is not a hexadecimal byte string.
 */
std::vector<std::uint8_t> ReadRegistryValueData(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    std::vector<std::uint8_t> data;
    std::string               error;
    if (!appbox::ParseRegistryHexText(appbox::UTF8ToWide(text), data, error))
    {
        throw appbox::ProjectDocumentError(error);
    }

    return data;
}

/**
 * @brief Read the isolation mode of a filesystem entry.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The isolation mode of the member.
 * @throw appbox::ProjectDocumentError The mode is unknown or not a string.
 */
appbox::FilesystemIsolation ReadFilesystemIsolation(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::FilesystemIsolation isolation = appbox::FilesystemIsolation::WriteCopy;
    if (!appbox::filesystem_isolation::ParseIsolationToken(text, isolation))
    {
        throw appbox::ProjectDocumentError("unknown isolation mode '" + text + "'");
    }

    return isolation;
}

/**
 * @brief Read the kind of a filesystem entry.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The kind of the member.
 * @throw appbox::ProjectDocumentError The kind is unknown or not a string.
 */
appbox::FilesystemEntryKind ReadFilesystemEntryKind(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::Directory;
    if (!appbox::filesystem_isolation::ParseEntryKindToken(text, kind))
    {
        throw appbox::ProjectDocumentError("unknown entry kind '" + text + "'");
    }

    return kind;
}

/**
 * @brief Read an optional member which holds an array of records.
 *
 * A member which is absent yields an empty list. Every element is converted
 * with the from_json() of the record type; the description of a failure is
 * prefixed with the path of the element inside the document, so the caller
 * knows which entry of the file was rejected.
 *
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @param[out] out Records of the member, appended in document order.
 * @throw appbox::ProjectDocumentError The member is not an array or one of its
 *        elements does not fit the schema.
 */
template <typename Record>
void ReadRecordArray(const Json& json, const char* key, std::vector<Record>& out)
{
    const auto member = json.find(key);
    if (member == json.end())
    {
        return;
    }
    if (!member->is_array())
    {
        throw appbox::ProjectDocumentError(std::string("the '") + key + "' member is not an array");
    }

    for (std::size_t index = 0; index < member->size(); ++index)
    {
        const auto scope = std::string(key) + "[" + std::to_string(index) + "]";
        try
        {
            out.push_back((*member)[index].get<Record>());
        }
        catch (const appbox::ProjectDocumentError& error)
        {
            throw appbox::ProjectDocumentError(scope + ": " + error.what());
        }
    }
}

/**
 * @brief Read an optional member which holds one nested record.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @param[out] out The record, reset when the member is absent or null.
 * @throw appbox::ProjectDocumentError The member does not fit the schema.
 */
template <typename Record>
void ReadOptionalRecord(const Json& json, const char* key, std::optional<Record>& out)
{
    out.reset();

    const auto member = json.find(key);
    if (member == json.end() || member->is_null())
    {
        return;
    }

    try
    {
        out = member->get<Record>();
    }
    catch (const appbox::ProjectDocumentError& error)
    {
        throw appbox::ProjectDocumentError(std::string(key) + ": " + error.what());
    }
}

} // namespace

namespace appbox
{

void to_json(nlohmann::ordered_json& json, const ProjectFolderRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kPresetKey] = record.preset_id;
    json[kNameKey] = WideToUTF8(record.name);
    json[kSourceKey] = WideToUTF8(record.source_path);
}

void from_json(const nlohmann::ordered_json& json, ProjectFolderRecord& record)
{
    RequireObject(json);

    ProjectFolderRecord candidate;
    candidate.preset_id = ReadRequiredString(json, kPresetKey);
    candidate.name = ReadRequiredText(json, kNameKey);
    candidate.source_path = ReadRequiredText(json, kSourceKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectFileRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kPresetKey] = record.preset_id;
    json[kTargetDirKey] = WideToUTF8(record.target_dir);
    json[kNameKey] = WideToUTF8(record.name);
    json[kSourceKey] = WideToUTF8(record.source_path);
}

void from_json(const nlohmann::ordered_json& json, ProjectFileRecord& record)
{
    RequireObject(json);

    ProjectFileRecord candidate;
    candidate.preset_id = ReadRequiredString(json, kPresetKey);
    candidate.target_dir = ReadRequiredText(json, kTargetDirKey);
    candidate.name = ReadRequiredText(json, kNameKey);
    candidate.source_path = ReadRequiredText(json, kSourceKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectMainProgramRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kPresetKey] = record.preset_id;
    json[kFolderKey] = WideToUTF8(record.folder);
    json[kPathKey] = WideToUTF8(record.relative_path);
}

void from_json(const nlohmann::ordered_json& json, ProjectMainProgramRecord& record)
{
    RequireObject(json);

    ProjectMainProgramRecord candidate;
    candidate.preset_id = ReadRequiredString(json, kPresetKey);
    candidate.folder = ReadRequiredText(json, kFolderKey);
    candidate.relative_path = ReadRequiredText(json, kPathKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectRegistryValueRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kNameKey] = WideToUTF8(record.name);
    json[kTypeKey] = WideToUTF8(RegistryValueTypeName(record.type));
    json[kDataKey] = WideToUTF8(FormatRegistryHexText(record.data));
    json[kIsolationKey] = registry_isolation::IsolationToken(record.isolation);
}

void from_json(const nlohmann::ordered_json& json, ProjectRegistryValueRecord& record)
{
    RequireObject(json);

    ProjectRegistryValueRecord candidate;
    candidate.name = ReadRequiredText(json, kNameKey);
    candidate.type = ReadRegistryValueType(json, kTypeKey);
    candidate.data = ReadRegistryValueData(json, kDataKey);
    candidate.isolation = ReadRegistryIsolation(json, kIsolationKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectRegistryKeyRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kNameKey] = WideToUTF8(record.name);
    json[kIsolationKey] = registry_isolation::IsolationToken(record.isolation);
    json[kValuesKey] = record.values;
    json[kChildrenKey] = record.children;
}

void from_json(const nlohmann::ordered_json& json, ProjectRegistryKeyRecord& record)
{
    RequireObject(json);

    ProjectRegistryKeyRecord candidate;
    candidate.name = ReadRequiredText(json, kNameKey);
    candidate.isolation = ReadRegistryIsolation(json, kIsolationKey);
    ReadRecordArray(json, kValuesKey, candidate.values);
    ReadRecordArray(json, kChildrenKey, candidate.children);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectFilesystemRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kPathKey] = WideToUTF8(record.path);
    json[kKindKey] = filesystem_isolation::EntryKindToken(record.kind);
    json[kIsolationKey] = filesystem_isolation::IsolationToken(record.isolation);
}

void from_json(const nlohmann::ordered_json& json, ProjectFilesystemRecord& record)
{
    RequireObject(json);

    ProjectFilesystemRecord candidate;
    candidate.path = ReadRequiredText(json, kPathKey);
    candidate.kind = ReadFilesystemEntryKind(json, kKindKey);
    candidate.isolation = ReadFilesystemIsolation(json, kIsolationKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectDocument& document)
{
    json = nlohmann::ordered_json::object();
    json[kVersionKey] = kProjectFileVersion;
    json[kOutputPathKey] = WideToUTF8(document.output_path);
    json[kFoldersKey] = document.folders;
    json[kFilesKey] = document.files;

    /* A document without a main program does not name the member at all. */
    if (document.main_program.has_value())
    {
        json[kMainProgramKey] = *document.main_program;
    }

    json[kRegistryKey] = document.registry;
    json[kFilesystemKey] = document.filesystem;
}

void from_json(const nlohmann::ordered_json& json, ProjectDocument& document)
{
    if (!json.is_object())
    {
        throw ProjectDocumentError("the project file does not hold a JSON object");
    }

    const auto version = json.find(kVersionKey);
    if (version == json.end() || !version->is_number_integer())
    {
        throw ProjectDocumentError(std::string("the project file has no '") + kVersionKey + "' number");
    }

    const auto version_value = version->get<int>();
    if (version_value != kProjectFileVersion)
    {
        throw ProjectDocumentError("unsupported project file version " + std::to_string(version_value) + " (expected " +
                                   std::to_string(kProjectFileVersion) + ")");
    }

    /*
     * The document is decoded into a local structure which replaces the caller
     * only when every member was accepted, so a rejected document leaves the
     * caller untouched.
     */
    ProjectDocument candidate;
    candidate.output_path = ReadOptionalText(json, kOutputPathKey);
    ReadRecordArray(json, kFoldersKey, candidate.folders);
    ReadRecordArray(json, kFilesKey, candidate.files);
    ReadOptionalRecord(json, kMainProgramKey, candidate.main_program);
    ReadRecordArray(json, kRegistryKey, candidate.registry);
    ReadRecordArray(json, kFilesystemKey, candidate.filesystem);

    document = std::move(candidate);
}

} // namespace appbox
