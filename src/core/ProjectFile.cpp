#include "ProjectFile.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace
{

/* Member names of the project file schema. */
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
constexpr const char* kRegistryKeysKey = "keys";
constexpr const char* kChildrenKey = "children";
constexpr const char* kValuesKey = "values";
constexpr const char* kTypeKey = "type";
constexpr const char* kDataKey = "data";
constexpr const char* kIsolationKey = "isolation";

/**
 * @brief Name of the encoding a byte order mark belongs to.
 *
 * A project file is UTF-8 text, so a UTF-16 or UTF-32 mark means the file was
 * written by a different tool. Such a file is rejected instead of being
 * decoded with replacement characters, which would turn every path into
 * unusable text.
 *
 * @param[in] text File content.
 * @return The encoding name, empty when no UTF-16 or UTF-32 mark is present.
 */
std::string ForeignEncoding(const std::string& text)
{
    const auto at = [&text](std::size_t index) { return static_cast<unsigned char>(text[index]); };

    if (text.size() >= 4)
    {
        const bool little = at(0) == 0xFF && at(1) == 0xFE && at(2) == 0x00 && at(3) == 0x00;
        const bool big = at(0) == 0x00 && at(1) == 0x00 && at(2) == 0xFE && at(3) == 0xFF;
        if (little || big)
        {
            return "UTF-32";
        }
    }

    if (text.size() >= 2)
    {
        const bool little = at(0) == 0xFF && at(1) == 0xFE;
        const bool big = at(0) == 0xFE && at(1) == 0xFF;
        if (little || big)
        {
            return "UTF-16";
        }
    }

    return {};
}

/**
 * @brief Whether the content starts with a UTF-8 byte order mark.
 * @param[in] text File content.
 * @return true when the mark is present.
 */
bool HasUtf8Bom(const std::string& text)
{
    return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
           && static_cast<unsigned char>(text[1]) == 0xBB
           && static_cast<unsigned char>(text[2]) == 0xBF;
}

/**
 * @brief Whether a byte sequence is well formed UTF-8.
 *
 * Overlong encodings, surrogate code points and values above U+10FFFF are
 * rejected as well, so a text which passes the check can be decoded without
 * any replacement character being introduced. The check is needed on top of
 * the JSON parser because a text encoded with a local code page may still
 * hold bytes which form valid JSON.
 *
 * @param[in] text Text to check.
 * @return true when the text is valid UTF-8.
 */
bool IsValidUTF8(const std::string& text)
{
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[index]);

        if (lead <= 0x7F)
        {
            ++index;
            continue;
        }

        std::size_t trailing = 0;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            trailing = 1;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            trailing = 2;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            trailing = 3;
        }
        else
        {
            /* A stray continuation byte, an overlong two byte form or a lead
             * byte above U+10FFFF. */
            return false;
        }

        if (index + trailing >= text.size())
        {
            return false;
        }

        /* The second byte is bounded tighter for the lead bytes whose range
         * would otherwise cover an overlong form or a surrogate code point. */
        const auto second = static_cast<unsigned char>(text[index + 1]);
        const auto lower = lead == 0xE0 ? 0xA0 : (lead == 0xF0 ? 0x90 : 0x80);
        const auto upper = lead == 0xED ? 0x9F : (lead == 0xF4 ? 0x8F : 0xBF);
        if (second < lower || second > upper)
        {
            return false;
        }

        for (std::size_t offset = 2; offset <= trailing; ++offset)
        {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if (continuation < 0x80 || continuation > 0xBF)
            {
                return false;
            }
        }

        index += trailing + 1;
    }

    return true;
}

/**
 * @brief Prefix an error description with the part of the file it belongs to.
 * @param[in] scope Name of the member which was being read.
 * @param[in] message Error description of the member.
 * @return The combined description.
 */
std::string Scoped(const std::string& scope, const std::string& message)
{
    return scope + ": " + message;
}

/**
 * @brief Read a required string member of a JSON object.
 * @param[in] object JSON object to read from.
 * @param[in] key Name of the member.
 * @param[in] scope Name of the part of the file for error descriptions.
 * @param[out] out Value of the member.
 * @param[out] error Error description on failure.
 * @return true when the member is present and holds a string.
 */
bool ReadString(const nlohmann::json& object, const char* key, const std::string& scope,
                std::string& out, std::string& error)
{
    if (!object.contains(key) || !object.at(key).is_string())
    {
        error = Scoped(scope, std::string("the '") + key + "' member is missing or not a string");
        return false;
    }

    out = object.at(key).get<std::string>();
    return true;
}

/**
 * @brief Build the JSON object of one key of the virtual registry.
 *
 * The object holds the name, the isolation mode and the values of the key and,
 * recursively, its sub keys. The data of a value is stored as a hexadecimal
 * byte string, which is the representation the model already uses to read and
 * write raw registry data.
 *
 * @param[in] key The key to store.
 * @return The JSON object of the key.
 */
nlohmann::ordered_json WriteRegistryKey(const appbox::RegistryKeyNode& key)
{
    nlohmann::ordered_json entry;
    entry[kNameKey] = appbox::WideToUTF8(key.name);
    entry[kIsolationKey] = appbox::registry_isolation::IsolationToken(key.isolation);

    nlohmann::ordered_json values = nlohmann::ordered_json::array();
    for (const auto& value : key.values)
    {
        nlohmann::ordered_json item;
        item[kNameKey] = appbox::WideToUTF8(value.name);
        item[kTypeKey] = appbox::WideToUTF8(appbox::RegistryValueTypeName(value.type));
        item[kDataKey] = appbox::WideToUTF8(appbox::FormatRegistryHexText(value.data));
        item[kIsolationKey] = appbox::registry_isolation::IsolationToken(value.isolation);
        values.push_back(std::move(item));
    }
    entry[kValuesKey] = std::move(values);

    nlohmann::ordered_json children = nlohmann::ordered_json::array();
    for (const auto& child : key.children)
    {
        children.push_back(WriteRegistryKey(child));
    }
    entry[kChildrenKey] = std::move(children);

    return entry;
}

/**
 * @brief Read one key of the virtual registry and its subtree.
 *
 * @param[in] element JSON object of the key.
 * @param[in] parent_path Path of the parent key, empty for a root key.
 * @param[in,out] model Model which receives the key.
 * @param[in] scope Name of the part of the file for error descriptions.
 * @param[out] error Error description on failure.
 * @return true when the key was read.
 */
bool DecodeRegistryKey(const nlohmann::json& element, const std::wstring& parent_path,
                       appbox::RegistryModel& model, const std::string& scope, std::string& error)
{
    if (!element.is_object())
    {
        error = Scoped(scope, "the entry is not a JSON object");
        return false;
    }

    std::string name;
    if (!ReadString(element, kNameKey, scope, name, error))
    {
        return false;
    }

    const auto key_name = appbox::UTF8ToWide(name);
    const auto path = appbox::JoinRegistryPath(parent_path, key_name);
    if (parent_path.empty())
    {
        /*
         * The first level holds the root keys of the view, which the model
         * creates itself: an unknown name would add a sixth root key.
         */
        if (model.FindKey(path) == nullptr)
        {
            error = Scoped(scope, "unknown root key '" + name + "'");
            return false;
        }
    }
    else
    {
        std::string detail;
        if (!model.AddKey(parent_path, key_name, detail))
        {
            error = Scoped(scope, detail);
            return false;
        }
    }

    std::string isolation_text;
    if (!ReadString(element, kIsolationKey, scope, isolation_text, error))
    {
        return false;
    }

    appbox::RegistryIsolation isolation = appbox::RegistryIsolation::WriteCopy;
    if (!appbox::registry_isolation::ParseIsolationToken(isolation_text, isolation))
    {
        error = Scoped(scope, "unknown isolation mode '" + isolation_text + "'");
        return false;
    }

    /*
     * The mode of the key is stored as it is; a project file which still holds
     * the `isolation_set` member of an older version is read as well, the
     * member is simply ignored.
     */
    if (!model.SetKeyIsolation(path, isolation))
    {
        error = Scoped(scope, "the key cannot be restored");
        return false;
    }

    if (element.contains(kValuesKey))
    {
        const auto& values = element.at(kValuesKey);
        if (!values.is_array())
        {
            error = Scoped(scope, std::string("the '") + kValuesKey + "' member is not an array");
            return false;
        }

        std::size_t index = 0;
        for (const auto& value : values)
        {
            const auto value_scope = scope + "." + kValuesKey + "[" + std::to_string(index) + "]";
            ++index;

            if (!value.is_object())
            {
                error = Scoped(value_scope, "the entry is not a JSON object");
                return false;
            }

            std::string value_name;
            std::string type_text;
            std::string data_text;
            std::string value_isolation_text;
            if (!ReadString(value, kNameKey, value_scope, value_name, error)
                || !ReadString(value, kTypeKey, value_scope, type_text, error)
                || !ReadString(value, kDataKey, value_scope, data_text, error)
                || !ReadString(value, kIsolationKey, value_scope, value_isolation_text, error))
            {
                return false;
            }

            appbox::RegistryValueType type = appbox::RegistryValueType::String;
            if (!appbox::ParseRegistryValueType(appbox::UTF8ToWide(type_text), type))
            {
                error = Scoped(value_scope, "unknown value type '" + type_text + "'");
                return false;
            }

            std::vector<std::uint8_t> data;
            std::string               hex_error;
            if (!appbox::ParseRegistryHexText(appbox::UTF8ToWide(data_text), data, hex_error))
            {
                error = Scoped(value_scope, hex_error);
                return false;
            }

            appbox::RegistryIsolation value_isolation = appbox::RegistryIsolation::WriteCopy;
            if (!appbox::registry_isolation::ParseIsolationToken(value_isolation_text, value_isolation))
            {
                error = Scoped(value_scope, "unknown isolation mode '" + value_isolation_text + "'");
                return false;
            }

            const auto stored_name = appbox::UTF8ToWide(value_name);
            std::string detail;
            if (!model.SetValue(path, stored_name, type, data, detail))
            {
                error = Scoped(value_scope, detail);
                return false;
            }
            if (!model.SetValueIsolation(path, stored_name, value_isolation))
            {
                error = Scoped(value_scope, "the value cannot be restored");
                return false;
            }
        }
    }

    if (element.contains(kChildrenKey))
    {
        const auto& children = element.at(kChildrenKey);
        if (!children.is_array())
        {
            error = Scoped(scope, std::string("the '") + kChildrenKey + "' member is not an array");
            return false;
        }

        std::size_t index = 0;
        for (const auto& child : children)
        {
            const auto child_scope = scope + "." + kChildrenKey + "[" + std::to_string(index) + "]";
            ++index;
            if (!DecodeRegistryKey(child, path, model, child_scope, error))
            {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Read the registry part of a project file.
 *
 * @param[in] element JSON object of the `registry` member.
 * @param[out] registry Registry replaced with the content of the member.
 * @param[out] error Error description on failure.
 * @return true when the registry was read.
 */
bool DecodeRegistry(const nlohmann::json& element, appbox::RegistryModel& registry, std::string& error)
{
    if (!element.is_object())
    {
        error = std::string("the '") + kRegistryKey + "' member is not a JSON object";
        return false;
    }

    appbox::RegistryModel candidate;
    const auto           keys = element.find(kRegistryKeysKey);
    if (keys != element.end())
    {
        if (!keys->is_array())
        {
            error = std::string("the '") + kRegistryKey + "." + kRegistryKeysKey + "' member is not an array";
            return false;
        }

        std::size_t index = 0;
        for (const auto& key : *keys)
        {
            const auto scope = std::string(kRegistryKey) + "." + kRegistryKeysKey + "[" + std::to_string(index) + "]";
            ++index;
            if (!DecodeRegistryKey(key, L"", candidate, scope, error))
            {
                return false;
            }
        }
    }

    registry = std::move(candidate);
    return true;
}

/**
 * @brief Decode the content of a project file.
 *
 * The configuration is built into a local model which is assigned to the
 * caller only when every entry was accepted, so a failure leaves the caller
 * untouched.
 *
 * @param[in] text Content of the project file.
 * @param[out] model Model replaced with the configuration of the file.
 * @param[out] registry Registry replaced with the registry of the file.
 * @param[out] output_path Destination archive path stored in the file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool DecodeProject(const std::string& text, appbox::PackModel& model, appbox::RegistryModel& registry,
                   std::wstring& output_path, std::string& error)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& ex)
    {
        error = std::string("the project file is not valid JSON: ") + ex.what();
        return false;
    }

    if (!root.is_object())
    {
        error = "the project file does not hold a JSON object";
        return false;
    }

    if (!root.contains(kVersionKey) || !root.at(kVersionKey).is_number_integer())
    {
        error = std::string("the project file has no '") + kVersionKey + "' number";
        return false;
    }

    const auto version = root.at(kVersionKey).get<int>();
    if (version != appbox::kProjectFileVersion)
    {
        error = "unsupported project file version " + std::to_string(version) + " (expected "
                + std::to_string(appbox::kProjectFileVersion) + ")";
        return false;
    }

    appbox::PackModel      candidate;
    appbox::RegistryModel  candidate_registry;
    std::wstring           candidate_output;

    if (root.contains(kOutputPathKey))
    {
        std::string value;
        if (!ReadString(root, kOutputPathKey, kOutputPathKey, value, error))
        {
            return false;
        }
        candidate_output = appbox::UTF8ToWide(value);
    }

    if (root.contains(kFoldersKey))
    {
        const auto& folders = root.at(kFoldersKey);
        if (!folders.is_array())
        {
            error = std::string("the '") + kFoldersKey + "' member is not an array";
            return false;
        }

        std::size_t index = 0;
        for (const auto& element : folders)
        {
            const auto scope = std::string(kFoldersKey) + "[" + std::to_string(index) + "]";
            ++index;

            if (!element.is_object())
            {
                error = Scoped(scope, "the entry is not a JSON object");
                return false;
            }

            std::string preset;
            std::string name;
            std::string source;
            if (!ReadString(element, kPresetKey, scope, preset, error)
                || !ReadString(element, kNameKey, scope, name, error)
                || !ReadString(element, kSourceKey, scope, source, error))
            {
                return false;
            }

            std::string detail;
            if (!candidate.RestoreImportedFolder(preset, appbox::UTF8ToWide(name),
                                                 appbox::UTF8ToWide(source), detail))
            {
                error = Scoped(scope, detail);
                return false;
            }
        }
    }

    if (root.contains(kFilesKey))
    {
        const auto& files = root.at(kFilesKey);
        if (!files.is_array())
        {
            error = std::string("the '") + kFilesKey + "' member is not an array";
            return false;
        }

        std::size_t index = 0;
        for (const auto& element : files)
        {
            const auto scope = std::string(kFilesKey) + "[" + std::to_string(index) + "]";
            ++index;

            if (!element.is_object())
            {
                error = Scoped(scope, "the entry is not a JSON object");
                return false;
            }

            std::string preset;
            std::string target_dir;
            std::string name;
            std::string source;
            if (!ReadString(element, kPresetKey, scope, preset, error)
                || !ReadString(element, kTargetDirKey, scope, target_dir, error)
                || !ReadString(element, kNameKey, scope, name, error)
                || !ReadString(element, kSourceKey, scope, source, error))
            {
                return false;
            }

            std::string detail;
            if (!candidate.RestoreImportedFile(preset, appbox::UTF8ToWide(target_dir),
                                               appbox::UTF8ToWide(name),
                                               appbox::UTF8ToWide(source), detail))
            {
                error = Scoped(scope, detail);
                return false;
            }
        }
    }

    if (root.contains(kMainProgramKey) && !root.at(kMainProgramKey).is_null())
    {
        const auto& element = root.at(kMainProgramKey);
        if (!element.is_object())
        {
            error = std::string("the '") + kMainProgramKey + "' member is not a JSON object";
            return false;
        }

        std::string preset;
        std::string folder;
        std::string relative;
        if (!ReadString(element, kPresetKey, kMainProgramKey, preset, error)
            || !ReadString(element, kFolderKey, kMainProgramKey, folder, error)
            || !ReadString(element, kPathKey, kMainProgramKey, relative, error))
        {
            return false;
        }

        std::string detail;
        if (!candidate.RestoreMainProgram(preset, appbox::UTF8ToWide(folder),
                                          appbox::UTF8ToWide(relative), detail))
        {
            error = Scoped(kMainProgramKey, detail);
            return false;
        }
    }

    if (root.contains(kRegistryKey))
    {
        if (!DecodeRegistry(root.at(kRegistryKey), candidate_registry, error))
        {
            return false;
        }
    }

    /* Every entry was accepted: the configuration can replace the caller. */
    model = std::move(candidate);
    registry = std::move(candidate_registry);
    output_path = std::move(candidate_output);
    return true;
}

} // namespace

namespace appbox
{

bool SaveProject(const PackModel& model, const RegistryModel& registry, const std::wstring& output_path,
                 const std::wstring& path, std::string& error)
{
    if (path.empty())
    {
        error = "no project file path was given";
        return false;
    }

    try
    {
        /*
         * The members are written in a fixed order (version first) so the file
         * stays easy to read and diff, which is why the ordered object is used
         * instead of the member name order of the default JSON object.
         */
        nlohmann::ordered_json root;
        root[kVersionKey] = kProjectFileVersion;
        root[kOutputPathKey] = WideToUTF8(output_path);

        nlohmann::ordered_json folders = nlohmann::ordered_json::array();
        for (const auto& preset : PresetDirectories())
        {
            for (const auto& imported : model.ImportsOf(preset.id))
            {
                nlohmann::ordered_json entry;
                entry[kPresetKey] = imported.preset_id;
                entry[kNameKey] = WideToUTF8(imported.import_name);
                entry[kSourceKey] = WideToUTF8(imported.source_path);
                folders.push_back(std::move(entry));
            }
        }
        root[kFoldersKey] = std::move(folders);

        nlohmann::ordered_json files = nlohmann::ordered_json::array();
        for (const auto& file : model.AllImportedFiles())
        {
            nlohmann::ordered_json entry;
            entry[kPresetKey] = file.preset_id;
            entry[kTargetDirKey] = WideToUTF8(file.target_dir);
            entry[kNameKey] = WideToUTF8(file.file_name);
            entry[kSourceKey] = WideToUTF8(file.source_path);
            files.push_back(std::move(entry));
        }
        root[kFilesKey] = std::move(files);

        /* The virtual registry of the workspace, root key by root key. */
        nlohmann::ordered_json registry_keys = nlohmann::ordered_json::array();
        for (const auto& key : registry.Root().children)
        {
            registry_keys.push_back(WriteRegistryKey(key));
        }
        nlohmann::ordered_json registry_entry;
        registry_entry[kRegistryKeysKey] = std::move(registry_keys);
        root[kRegistryKey] = std::move(registry_entry);

        if (model.HasMainProgram())
        {
            nlohmann::ordered_json entry;
            entry[kPresetKey] = model.MainProgramChoice().preset_id;
            entry[kFolderKey] = WideToUTF8(model.MainProgramChoice().import_name);
            entry[kPathKey] = WideToUTF8(model.MainProgramChoice().relative_path);
            root[kMainProgramKey] = std::move(entry);
        }

        /* dump() escapes nothing by default, so paths keep their UTF-8 bytes. */
        const auto text = root.dump(2);

        std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            error = "cannot create the project file: " + WideToUTF8(path);
            return false;
        }

        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good())
        {
            error = "cannot write the project file: " + WideToUTF8(path);
            return false;
        }
    }
    catch (const std::exception& ex)
    {
        error = std::string("cannot write the project file: ") + ex.what();
        return false;
    }

    return true;
}

bool SaveProject(const PackModel& model, const std::wstring& output_path, const std::wstring& path,
                 std::string& error)
{
    /* A caller without a registry stores an empty one. */
    return SaveProject(model, RegistryModel{}, output_path, path, error);
}

bool LoadProject(const std::wstring& path, PackModel& model, RegistryModel& registry,
                 std::wstring& output_path, std::string& error)
{
    if (path.empty())
    {
        error = "no project file path was given";
        return false;
    }

    std::string text;
    try
    {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (!in.is_open())
        {
            error = "cannot open the project file: " + WideToUTF8(path);
            return false;
        }

        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (in.bad())
        {
            error = "cannot read the project file: " + WideToUTF8(path);
            return false;
        }
    }
    catch (const std::exception& ex)
    {
        error = std::string("cannot read the project file: ") + ex.what();
        return false;
    }

    const auto encoding = ForeignEncoding(text);
    if (!encoding.empty())
    {
        error = "the project file is not UTF-8 encoded (a " + encoding + " byte order mark was found)";
        return false;
    }

    if (HasUtf8Bom(text))
    {
        text.erase(0, 3);
    }

    if (text.empty())
    {
        error = "the project file is empty";
        return false;
    }

    if (!IsValidUTF8(text))
    {
        error = "the project file is not valid UTF-8 text";
        return false;
    }

    try
    {
        return DecodeProject(text, model, registry, output_path, error);
    }
    catch (const std::exception& ex)
    {
        error = std::string("the project file cannot be decoded: ") + ex.what();
        return false;
    }
}

bool LoadProject(const std::wstring& path, PackModel& model, std::wstring& output_path,
                 std::string& error)
{
    /* The registry of the file is validated, but a caller without a registry
     * has no place to keep it. */
    RegistryModel registry;
    return LoadProject(path, model, registry, output_path, error);
}

} // namespace appbox
