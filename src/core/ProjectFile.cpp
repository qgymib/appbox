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
    return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
           static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
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
 * @brief Prefix an error description with the part of the document it belongs to.
 * @param[in] scope Path of the entry which was being applied.
 * @param[in] message Error description of the entry.
 * @return The combined description.
 */
std::string Scoped(const std::string& scope, const std::string& message)
{
    return scope + ": " + message;
}

/**
 * @brief Build the record of one key of the virtual registry.
 *
 * The values and the sub keys are stored in the order the model keeps them in,
 * which is the order the tree and the table show.
 *
 * @param[in] key The key to store.
 * @return The record of the key.
 */
appbox::ProjectRegistryKeyRecord MakeRegistryKeyRecord(const appbox::RegistryKeyNode& key)
{
    appbox::ProjectRegistryKeyRecord record;
    record.name = key.name;
    record.isolation = key.isolation;

    for (const auto& value : key.values)
    {
        appbox::ProjectRegistryValueRecord value_record;
        value_record.name = value.name;
        value_record.type = value.type;
        value_record.data = value.data;
        value_record.isolation = value.isolation;
        record.values.push_back(std::move(value_record));
    }

    for (const auto& child : key.children)
    {
        record.children.push_back(MakeRegistryKeyRecord(child));
    }

    return record;
}

/**
 * @brief Restore one key of the virtual registry and its subtree.
 *
 * The first level holds the root keys of the view, which the model creates
 * itself: a name which is not one of them is rejected instead of adding a
 * sixth root key.
 *
 * @param[in] record The key to restore.
 * @param[in] parent_path Path of the parent key, empty for a root key.
 * @param[in,out] model Model which receives the key.
 * @param[in] scope Path of the entry inside the document.
 * @param[out] error Error description on failure.
 * @return true when the key was restored.
 */
bool ApplyRegistryKey(const appbox::ProjectRegistryKeyRecord& record, const std::wstring& parent_path,
                      appbox::RegistryModel& model, const std::string& scope, std::string& error)
{
    const auto path = appbox::JoinRegistryPath(parent_path, record.name);

    if (parent_path.empty())
    {
        if (model.FindKey(path) == nullptr)
        {
            error = Scoped(scope, "unknown root key '" + appbox::WideToUTF8(record.name) + "'");
            return false;
        }
    }
    else
    {
        std::string detail;
        if (!model.AddKey(parent_path, record.name, detail))
        {
            error = Scoped(scope, detail);
            return false;
        }
    }

    /*
     * The mode of the key is stored as it is; a document which holds the member
     * of an entry which never set a mode is read with the mode of the key.
     */
    if (!model.SetKeyIsolation(path, record.isolation))
    {
        error = Scoped(scope, "the key cannot be restored");
        return false;
    }

    std::size_t index = 0;
    for (const auto& value : record.values)
    {
        const auto value_scope = scope + ".values[" + std::to_string(index) + "]";
        ++index;

        std::string detail;
        if (!model.SetValue(path, value.name, value.type, value.data, detail))
        {
            error = Scoped(value_scope, detail);
            return false;
        }
        if (!model.SetValueIsolation(path, value.name, value.isolation))
        {
            error = Scoped(value_scope, "the value cannot be restored");
            return false;
        }
    }

    index = 0;
    for (const auto& child : record.children)
    {
        const auto child_scope = scope + ".children[" + std::to_string(index) + "]";
        ++index;
        if (!ApplyRegistryKey(child, path, model, child_scope, error))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Restore the registry of a document.
 * @param[in] keys Root keys of the document.
 * @param[in,out] model Model which receives the keys.
 * @param[out] error Error description on failure.
 * @return true when the registry was restored.
 */
bool ApplyRegistryKeys(const std::vector<appbox::ProjectRegistryKeyRecord>& keys, appbox::RegistryModel& model,
                       std::string& error)
{
    std::size_t index = 0;
    for (const auto& key : keys)
    {
        const auto scope = "registry[" + std::to_string(index) + "]";
        ++index;
        if (!ApplyRegistryKey(key, L"", model, scope, error))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Restore the isolation modes of the virtual filesystem of a document.
 * @param[in] records Entries of the document.
 * @param[in,out] model Model which receives the modes.
 * @param[out] error Error description on failure.
 * @return true when the modes were restored.
 */
bool ApplyFilesystemRecords(const std::vector<appbox::ProjectFilesystemRecord>& records,
                            appbox::FilesystemIsolationModel& model, std::string& error)
{
    std::size_t index = 0;
    for (const auto& record : records)
    {
        const auto scope = "filesystem[" + std::to_string(index) + "]";
        ++index;

        appbox::FilesystemIsolationEntry entry;
        entry.path = record.path;
        entry.kind = record.kind;
        entry.isolation = record.isolation;

        std::string detail;
        if (!model.AddEntry(entry, detail))
        {
            error = Scoped(scope, detail);
            return false;
        }
    }

    return true;
}

} // namespace

namespace appbox
{

ProjectDocument MakeProjectDocument(const PackModel& model, const RegistryModel& registry,
                                    const FilesystemIsolationModel& isolation, const std::wstring& output_path)
{
    ProjectDocument document;
    document.output_path = output_path;

    for (const auto& preset : PresetDirectories())
    {
        for (const auto& imported : model.ImportsOf(preset.id))
        {
            ProjectFolderRecord record;
            record.preset_id = imported.preset_id;
            record.name = imported.import_name;
            record.source_path = imported.source_path;
            document.folders.push_back(std::move(record));
        }
    }

    for (const auto& file : model.AllImportedFiles())
    {
        ProjectFileRecord record;
        record.preset_id = file.preset_id;
        record.target_dir = file.target_dir;
        record.name = file.file_name;
        record.source_path = file.source_path;
        document.files.push_back(std::move(record));
    }

    if (model.HasMainProgram())
    {
        ProjectMainProgramRecord record;
        record.preset_id = model.MainProgramChoice().preset_id;
        record.folder = model.MainProgramChoice().import_name;
        record.relative_path = model.MainProgramChoice().relative_path;
        document.main_program = std::move(record);
    }

    for (const auto& key : registry.Root().children)
    {
        document.registry.push_back(MakeRegistryKeyRecord(key));
    }

    for (const auto& entry : isolation.Entries())
    {
        ProjectFilesystemRecord record;
        record.path = entry.path;
        record.kind = entry.kind;
        record.isolation = entry.isolation;
        document.filesystem.push_back(std::move(record));
    }

    return document;
}

bool ApplyProjectDocument(const ProjectDocument& document, PackModel& model, RegistryModel& registry,
                          FilesystemIsolationModel& isolation, std::wstring& output_path, std::string& error)
{
    PackModel                candidate;
    RegistryModel            candidate_registry;
    FilesystemIsolationModel candidate_isolation;

    std::size_t index = 0;
    for (const auto& folder : document.folders)
    {
        const auto scope = "folders[" + std::to_string(index) + "]";
        ++index;

        std::string detail;
        if (!candidate.RestoreImportedFolder(folder.preset_id, folder.name, folder.source_path, detail))
        {
            error = Scoped(scope, detail);
            return false;
        }
    }

    index = 0;
    for (const auto& file : document.files)
    {
        const auto scope = "files[" + std::to_string(index) + "]";
        ++index;

        std::string detail;
        if (!candidate.RestoreImportedFile(file.preset_id, file.target_dir, file.name, file.source_path, detail))
        {
            error = Scoped(scope, detail);
            return false;
        }
    }

    if (document.main_program.has_value())
    {
        const auto& program = *document.main_program;

        std::string detail;
        if (!candidate.RestoreMainProgram(program.preset_id, program.folder, program.relative_path, detail))
        {
            error = Scoped("main_program", detail);
            return false;
        }
    }

    if (!ApplyRegistryKeys(document.registry, candidate_registry, error))
    {
        return false;
    }

    if (!ApplyFilesystemRecords(document.filesystem, candidate_isolation, error))
    {
        return false;
    }

    /* Every entry was accepted: the document can replace the caller. */
    model = std::move(candidate);
    registry = std::move(candidate_registry);
    isolation = std::move(candidate_isolation);
    output_path = document.output_path;
    return true;
}

bool SaveProject(const ProjectDocument& document, const std::wstring& path, std::string& error)
{
    if (path.empty())
    {
        error = "no project file path was given";
        return false;
    }

    try
    {
        /* dump() escapes nothing by default, so paths keep their UTF-8 bytes. */
        const auto text = nlohmann::ordered_json(document).dump(2);

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

bool LoadProject(const std::wstring& path, ProjectDocument& document, std::string& error)
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
        const auto root = nlohmann::ordered_json::parse(text);
        document = root.get<ProjectDocument>();
        return true;
    }
    catch (const nlohmann::json::exception& ex)
    {
        error = std::string("the project file is not valid JSON: ") + ex.what();
        return false;
    }
    catch (const ProjectDocumentError& ex)
    {
        /* The conversion describes the member which does not fit the schema. */
        error = ex.what();
        return false;
    }
    catch (const std::exception& ex)
    {
        error = std::string("the project file cannot be decoded: ") + ex.what();
        return false;
    }
}

} // namespace appbox
