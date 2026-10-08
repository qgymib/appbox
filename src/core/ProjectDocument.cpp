#include "ProjectDocument.hpp"
#include "ApplicationMetadata.hpp"
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
constexpr const char* kProjectTypeKey = "project_type";
constexpr const char* kMetadataKey = "metadata";
constexpr const char* kOverridesKey = "overrides";
constexpr const char* kKeyKey = "key";
constexpr const char* kFoldersKey = "folders";
constexpr const char* kFilesKey = "files";
constexpr const char* kStartupFilesKey = "startup_files";
constexpr const char* kPresetKey = "preset";
constexpr const char* kNameKey = "name";
constexpr const char* kSourceKey = "source";
constexpr const char* kTargetDirKey = "target_dir";
constexpr const char* kFolderKey = "folder";
constexpr const char* kPathKey = "path";
constexpr const char* kTriggerKey = "trigger";
constexpr const char* kAutoStartKey = "auto_start";

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

/* Member names of the network part of the schema. */
constexpr const char* kNetworkKey = "network";
constexpr const char* kHostnameKey = "hostname";
constexpr const char* kRedirectKey = "redirect";
constexpr const char* kProxyKey = "proxy";
constexpr const char* kTcpKey = "tcp";
constexpr const char* kUdpKey = "udp";
constexpr const char* kServerKey = "server";
constexpr const char* kPortKey = "port";
constexpr const char* kUsernameKey = "username";
constexpr const char* kPasswordKey = "password";

/* Member names of the environment part of the schema. */
constexpr const char* kEnvironmentKey = "environment";
constexpr const char* kValueKey = "value";
constexpr const char* kMergeKey = "merge";
constexpr const char* kMergeStringKey = "merge_string";

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
 * @brief Read a required member which holds a boolean.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The value of the member.
 * @throw appbox::ProjectDocumentError The member is missing or not a boolean.
 */
bool ReadRequiredBool(const Json& json, const char* key)
{
    const auto member = json.find(key);
    if (member == json.end() || !member->is_boolean())
    {
        throw appbox::ProjectDocumentError(std::string("the '") + key + "' member is missing or not a boolean");
    }

    return member->get<bool>();
}

/**
 * @brief Read the project type of a document.
 *
 * A document which does not name the member describes a standalone project,
 * which is what a file written before the member existed describes.
 *
 * @param[in] json Object to read from.
 * @return The project type of the member.
 * @throw appbox::ProjectDocumentError The member is present but not a known
 *        project type.
 */
appbox::ProjectType ReadProjectType(const Json& json)
{
    const auto member = json.find(kProjectTypeKey);
    if (member == json.end())
    {
        return appbox::ProjectType::Standalone;
    }
    if (!member->is_string())
    {
        throw appbox::ProjectDocumentError(std::string("the '") + kProjectTypeKey + "' member is not a string");
    }

    const auto          text = member->get<std::string>();
    appbox::ProjectType type = appbox::ProjectType::Standalone;
    if (!appbox::ParseProjectTypeToken(text, type))
    {
        throw appbox::ProjectDocumentError("unknown project type '" + text + "'");
    }

    return type;
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
 * @brief Read the protocol of the proxy of a document.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The protocol of the member.
 * @throw appbox::ProjectDocumentError The protocol is unknown or not a string.
 */
appbox::ProxyType ReadProxyType(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::ProxyType type = appbox::ProxyType::Socks5;
    if (!appbox::ParseProxyTypeToken(text, type))
    {
        throw appbox::ProjectDocumentError("unknown proxy type '" + text + "'");
    }

    return type;
}

/**
 * @brief Read the isolation mode of an environment variable.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The isolation mode of the member.
 * @throw appbox::ProjectDocumentError The mode is unknown or not a string.
 */
appbox::EnvironmentIsolation ReadEnvironmentIsolation(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::EnvironmentIsolation isolation = appbox::EnvironmentIsolation::WriteCopy;
    if (!appbox::environment_isolation::ParseIsolationToken(text, isolation))
    {
        throw appbox::ProjectDocumentError("unknown isolation mode '" + text + "'");
    }

    return isolation;
}

/**
 * @brief Read the merge mode of an environment variable.
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @return The merge mode of the member.
 * @throw appbox::ProjectDocumentError The mode is unknown or not a string.
 */
appbox::EnvironmentMergeMode ReadEnvironmentMergeMode(const Json& json, const char* key)
{
    const auto text = ReadRequiredString(json, key);

    appbox::EnvironmentMergeMode merge = appbox::environment_isolation::kDefaultMergeMode;
    if (!appbox::environment_isolation::ParseMergeModeToken(text, merge))
    {
        throw appbox::ProjectDocumentError("unknown merge mode '" + text + "'");
    }

    return merge;
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
 * @brief Read a member which holds one nested record, absent or not.
 *
 * A member which is absent or null leaves @p out at the value it carries, so a
 * record of an optional member keeps the default of a fresh document.
 *
 * @param[in] json Object to read from.
 * @param[in] key Name of the member.
 * @param[out] out The record, untouched when the member is absent or null.
 * @throw appbox::ProjectDocumentError The member does not fit the schema.
 */
template <typename Record>
void ReadRecord(const Json& json, const char* key, Record& out)
{
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

void to_json(nlohmann::ordered_json& json, const ProjectMetadataFieldRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kKeyKey] = record.key;
    json[kValueKey] = WideToUTF8(record.value);
}

void from_json(const nlohmann::ordered_json& json, ProjectMetadataFieldRecord& record)
{
    RequireObject(json);

    ProjectMetadataFieldRecord candidate;
    candidate.key = ReadRequiredString(json, kKeyKey);

    /* Only the fields of a version resource can be written into a launcher. */
    if (!IsMetadataField(candidate.key))
    {
        throw ProjectDocumentError("unknown metadata field '" + candidate.key + "'");
    }

    candidate.value = ReadRequiredText(json, kValueKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectMetadataRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kSourceKey] = WideToUTF8(record.source);
    json[kOverridesKey] = record.overrides;
}

void from_json(const nlohmann::ordered_json& json, ProjectMetadataRecord& record)
{
    RequireObject(json);

    ProjectMetadataRecord candidate;

    /*
     * A document which does not name the source describes the default one, so
     * a hand written document only has to list the fields it edits.
     */
    candidate.source = ReadOptionalText(json, kSourceKey);
    ReadRecordArray(json, kOverridesKey, candidate.overrides);

    /* A field which is listed twice would be written twice as well. */
    for (std::size_t index = 0; index < candidate.overrides.size(); ++index)
    {
        for (std::size_t other = index + 1; other < candidate.overrides.size(); ++other)
        {
            if (candidate.overrides[index].key == candidate.overrides[other].key)
            {
                throw ProjectDocumentError("overrides[" + std::to_string(other) + "]: the field '" +
                                           candidate.overrides[other].key + "' is listed twice");
            }
        }
    }

    record = std::move(candidate);
}

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

void to_json(nlohmann::ordered_json& json, const ProjectStartupRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kPresetKey] = record.preset_id;
    json[kFolderKey] = WideToUTF8(record.folder);
    json[kPathKey] = WideToUTF8(record.relative_path);
    json[kTriggerKey] = WideToUTF8(record.trigger);
    json[kAutoStartKey] = record.auto_start;
}

void from_json(const nlohmann::ordered_json& json, ProjectStartupRecord& record)
{
    RequireObject(json);

    ProjectStartupRecord candidate;
    candidate.preset_id = ReadRequiredString(json, kPresetKey);
    candidate.folder = ReadRequiredText(json, kFolderKey);
    candidate.relative_path = ReadRequiredText(json, kPathKey);
    candidate.trigger = ReadRequiredText(json, kTriggerKey);
    candidate.auto_start = ReadRequiredBool(json, kAutoStartKey);

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

void to_json(nlohmann::ordered_json& json, const ProjectDnsRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kHostnameKey] = WideToUTF8(record.hostname);
    json[kRedirectKey] = WideToUTF8(record.redirect);
}

void from_json(const nlohmann::ordered_json& json, ProjectDnsRecord& record)
{
    RequireObject(json);

    ProjectDnsRecord candidate;
    candidate.hostname = ReadRequiredText(json, kHostnameKey);
    candidate.redirect = ReadRequiredText(json, kRedirectKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectProxyRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kTypeKey] = ProxyTypeToken(record.type);
    json[kTcpKey] = record.tcp;
    json[kUdpKey] = record.udp;
    json[kServerKey] = WideToUTF8(record.server);
    json[kPortKey] = WideToUTF8(record.port);
    json[kUsernameKey] = WideToUTF8(record.username);
    json[kPasswordKey] = WideToUTF8(record.password);
}

void from_json(const nlohmann::ordered_json& json, ProjectProxyRecord& record)
{
    RequireObject(json);

    ProjectProxyRecord candidate;
    candidate.type = ReadProxyType(json, kTypeKey);
    candidate.tcp = ReadRequiredBool(json, kTcpKey);
    candidate.udp = ReadRequiredBool(json, kUdpKey);
    candidate.server = ReadRequiredText(json, kServerKey);
    candidate.port = ReadRequiredText(json, kPortKey);
    candidate.username = ReadRequiredText(json, kUsernameKey);
    candidate.password = ReadRequiredText(json, kPasswordKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectEnvironmentRecord& record)
{
    json = nlohmann::ordered_json::object();
    json[kNameKey] = WideToUTF8(record.name);
    json[kValueKey] = WideToUTF8(record.value);
    json[kIsolationKey] = environment_isolation::IsolationToken(record.isolation);
    json[kMergeKey] = environment_isolation::MergeModeToken(record.merge);
    json[kMergeStringKey] = WideToUTF8(record.merge_string);
}

void from_json(const nlohmann::ordered_json& json, ProjectEnvironmentRecord& record)
{
    RequireObject(json);

    ProjectEnvironmentRecord candidate;
    candidate.name = ReadRequiredText(json, kNameKey);
    candidate.value = ReadRequiredText(json, kValueKey);
    candidate.isolation = ReadEnvironmentIsolation(json, kIsolationKey);
    candidate.merge = ReadEnvironmentMergeMode(json, kMergeKey);
    candidate.merge_string = ReadRequiredText(json, kMergeStringKey);

    record = std::move(candidate);
}

void to_json(nlohmann::ordered_json& json, const ProjectDocument& document)
{
    json = nlohmann::ordered_json::object();
    json[kVersionKey] = kProjectFileVersion;
    json[kOutputPathKey] = WideToUTF8(document.output_path);
    json[kProjectTypeKey] = ProjectTypeToken(document.project_type);
    json[kMetadataKey] = document.metadata;
    json[kFoldersKey] = document.folders;
    json[kFilesKey] = document.files;
    json[kStartupFilesKey] = document.startup_files;
    json[kRegistryKey] = document.registry;
    json[kFilesystemKey] = document.filesystem;
    json[kNetworkKey] = document.network;

    /* A document without a proxy does not name the member at all. */
    if (document.proxy.has_value())
    {
        json[kProxyKey] = *document.proxy;
    }

    json[kEnvironmentKey] = document.environment;
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
    candidate.project_type = ReadProjectType(json);
    ReadRecord(json, kMetadataKey, candidate.metadata);
    ReadRecordArray(json, kFoldersKey, candidate.folders);
    ReadRecordArray(json, kFilesKey, candidate.files);
    ReadRecordArray(json, kStartupFilesKey, candidate.startup_files);
    ReadRecordArray(json, kRegistryKey, candidate.registry);
    ReadRecordArray(json, kFilesystemKey, candidate.filesystem);
    ReadRecordArray(json, kNetworkKey, candidate.network);
    ReadOptionalRecord(json, kProxyKey, candidate.proxy);
    ReadRecordArray(json, kEnvironmentKey, candidate.environment);

    document = std::move(candidate);
}

} // namespace appbox
