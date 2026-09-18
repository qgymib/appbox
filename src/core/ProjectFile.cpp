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
 * @brief Decode the content of a project file.
 *
 * The configuration is built into a local model which is assigned to the
 * caller only when every entry was accepted, so a failure leaves the caller
 * untouched.
 *
 * @param[in] text Content of the project file.
 * @param[out] model Model replaced with the configuration of the file.
 * @param[out] output_path Destination archive path stored in the file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool DecodeProject(const std::string& text, appbox::PackModel& model, std::wstring& output_path,
                   std::string& error)
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

    appbox::PackModel candidate;
    std::wstring candidate_output;

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

    /* Every entry was accepted: the configuration can replace the caller. */
    model = std::move(candidate);
    output_path = std::move(candidate_output);
    return true;
}

} // namespace

namespace appbox
{

bool SaveProject(const PackModel& model, const std::wstring& output_path, const std::wstring& path,
                 std::string& error)
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

bool LoadProject(const std::wstring& path, PackModel& model, std::wstring& output_path,
                 std::string& error)
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
        return DecodeProject(text, model, output_path, error);
    }
    catch (const std::exception& ex)
    {
        error = std::string("the project file cannot be decoded: ") + ex.what();
        return false;
    }
}

} // namespace appbox
