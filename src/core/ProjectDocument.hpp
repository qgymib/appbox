#ifndef APPBOX_PACKER_CORE_PROJECT_DOCUMENT_HPP
#define APPBOX_PACKER_CORE_PROJECT_DOCUMENT_HPP

#include "FilesystemIsolation.hpp"
#include "RegistryIsolation.hpp"
#include "RegistryModel.hpp"
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Version of the project file schema.
 *
 * The version is the first member of every project file and is required to
 * match exactly when a file is read, so a file of a future format is rejected
 * instead of being interpreted with the rules of the current one.
 */
inline constexpr int kProjectFileVersion = 1;

/**
 * @brief Error reported by the conversion of a project file document.
 *
 * The class carries the description of the failure as it is shown to the user:
 * a missing or mistyped member, an unknown token or a version which is not
 * supported. The text of the exception is the final error description, so the
 * caller does not have to wrap it again.
 *
 * The conversion of an array element prefixes the description with the path of
 * the element inside the document, for example `folders[1]: ...`, and a member
 * of a nested entry keeps its own path, so a value of the third root key is
 * reported as `registry[2]: values[0]: ...`.
 */
class ProjectDocumentError : public std::runtime_error
{
public:
    /**
     * @brief Create the error with the description of the failure.
     * @param[in] message Description of the failure, shown to the user.
     */
    explicit ProjectDocumentError(const std::string& message) : std::runtime_error(message)
    {
    }
};

/**
 * @brief One imported folder of a project document.
 *
 * At sandbox runtime the folder content is visible as
 * `<preset>\<name>` inside the sandbox view.
 */
struct ProjectFolderRecord
{
    /**
     * @brief Identifier of the owning preset directory.
     */
    std::string preset_id;

    /**
     * @brief Subdirectory name below the preset directory.
     */
    std::wstring name;

    /**
     * @brief Host folder which was imported.
     */
    std::wstring source_path;
};

/**
 * @brief One individually imported file of a project document.
 *
 * At sandbox runtime the file is visible as
 * `<preset>\<target_dir>\<name>` inside the sandbox view.
 */
struct ProjectFileRecord
{
    /**
     * @brief Identifier of the owning preset directory.
     */
    std::string preset_id;

    /**
     * @brief Directory relative to the preset directory, e.g. `L"MyApp\data"`.
     */
    std::wstring target_dir;

    /**
     * @brief Name of the file inside the target directory.
     */
    std::wstring name;

    /**
     * @brief Host file which was imported.
     */
    std::wstring source_path;
};

/**
 * @brief The main program of a project document.
 */
struct ProjectMainProgramRecord
{
    /**
     * @brief Identifier of the preset directory owning the imported folder.
     */
    std::string preset_id;

    /**
     * @brief Imported folder containing the executable.
     */
    std::wstring folder;

    /**
     * @brief Executable path relative to the imported folder root.
     */
    std::wstring relative_path;
};

/**
 * @brief One value of a registry key of a project document.
 *
 * The data holds the raw bytes of the value, which is the representation the
 * registry API uses; the document stores it as a hexadecimal byte string.
 */
struct ProjectRegistryValueRecord
{
    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::wstring name;

    /**
     * @brief Type of the value.
     */
    RegistryValueType type = RegistryValueType::String;

    /**
     * @brief Raw data of the value.
     */
    std::vector<std::uint8_t> data;

    /**
     * @brief Isolation mode of the value.
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;
};

/**
 * @brief One key of the registry part of a project document.
 *
 * The keys of the document form the tree of the virtual registry: the document
 * holds one record per root key and every record holds its sub keys, so a key
 * is stored with everything below it.
 */
struct ProjectRegistryKeyRecord
{
    /**
     * @brief Name of the key relative to its parent.
     */
    std::wstring name;

    /**
     * @brief Isolation mode of the key.
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;

    /**
     * @brief Values of the key, in document order.
     */
    std::vector<ProjectRegistryValueRecord> values;

    /**
     * @brief Sub keys of the key, in document order.
     */
    std::vector<ProjectRegistryKeyRecord> children;
};

/**
 * @brief One entry of the filesystem isolation part of a project document.
 *
 * The record names a path of the virtual filesystem - the string the
 * `Source Path` column shows, for example `#ProgramFiles#\MyApp` - together
 * with the kind of the entry and the mode the user picked for it.
 */
struct ProjectFilesystemRecord
{
    /**
     * @brief Path of the entry inside the virtual filesystem.
     */
    std::wstring path;

    /**
     * @brief Kind of the entry the mode was picked for.
     */
    FilesystemEntryKind kind = FilesystemEntryKind::Directory;

    /**
     * @brief Isolation mode the user picked for the entry.
     */
    FilesystemIsolation isolation = FilesystemIsolation::WriteCopy;
};

/**
 * @brief The content of a project file.
 *
 * The structure is the document of the `File -> Export Configuration...` and
 * `File -> Import Configuration...` commands: the imported folders and files,
 * the main program, the virtual registry and the isolation modes of the
 * virtual filesystem. It holds no host state and no wxWidgets dependency, so
 * the conversion is unit testable.
 *
 * `to_json()` and `from_json()` convert the structure to and from the JSON
 * text of a project file; the file itself is written and read by
 * `src/core/ProjectFile.*`, which also maps the structure to the models of the
 * workspace. The members of the document are written in the order of the
 * structure, so a file which is written from a document is stable and easy to
 * read and diff.
 *
 * The schema is:
 *
 * ```
 * {
 *   "version": 1,
 *   "output_path": "D:\\out\\MyApp.zip",
 *   "folders": [ { "preset": "program_files", "name": "MyApp",
 *                  "source": "C:\\Program Files\\MyApp" } ],
 *   "files": [ { "preset": "user_profile", "target_dir": "MyApp\\data",
 *                "name": "settings.ini", "source": "C:\\tmp\\settings.ini" } ],
 *   "main_program": { "preset": "program_files", "folder": "MyApp",
 *                     "path": "bin\\app.exe" },
 *   "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "full",
 *                   "values": [ { "name": "Server", "type": "REG_SZ",
 *                                 "data": "68 00 65 00 6C 00 6C 00 6F 00",
 *                                 "isolation": "write_copy" } ],
 *                   "children": [] } ],
 *   "filesystem": [ { "path": "#ProgramFiles#\\MyApp", "kind": "directory",
 *                     "isolation": "whiteout" } ]
 * }
 * ```
 *
 * Every path is stored as the host path it has on the machine which exported
 * the configuration; the file only records the imports, it never copies the
 * imported content itself.
 */
struct ProjectDocument
{
    /**
     * @brief Path of the `Output File` box, empty when none was chosen.
     */
    std::wstring output_path;

    /**
     * @brief Imported folders, in the order the document stores them.
     */
    std::vector<ProjectFolderRecord> folders;

    /**
     * @brief Individually imported files, in the order the document stores them.
     */
    std::vector<ProjectFileRecord> files;

    /**
     * @brief The selected main program, absent while none is selected.
     */
    std::optional<ProjectMainProgramRecord> main_program;

    /**
     * @brief The root keys of the virtual registry with their subtree.
     */
    std::vector<ProjectRegistryKeyRecord> registry;

    /**
     * @brief The isolation modes of the virtual filesystem.
     */
    std::vector<ProjectFilesystemRecord> filesystem;
};

/**
 * @brief Store one imported folder of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectFolderRecord& record);

/**
 * @brief Read one imported folder of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectFolderRecord& record);

/**
 * @brief Store one imported file of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectFileRecord& record);

/**
 * @brief Read one imported file of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectFileRecord& record);

/**
 * @brief Store the main program of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectMainProgramRecord& record);

/**
 * @brief Read the main program of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectMainProgramRecord& record);

/**
 * @brief Store one value of a registry key of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectRegistryValueRecord& record);

/**
 * @brief Read one value of a registry key of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectRegistryValueRecord& record);

/**
 * @brief Store one key of the registry part of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectRegistryKeyRecord& record);

/**
 * @brief Read one key of the registry part of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectRegistryKeyRecord& record);

/**
 * @brief Store one filesystem isolation entry of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectFilesystemRecord& record);

/**
 * @brief Read one filesystem isolation entry of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectFilesystemRecord& record);

/**
 * @brief Store the content of a project file as a JSON document.
 *
 * The members are written in the order of ProjectDocument with the schema
 * version first, so the text of a given document is stable and easy to read.
 * Every member is written, except `main_program`, which is omitted while no
 * main program is selected.
 *
 * @param[out] json Object which receives the document.
 * @param[in] document The document to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectDocument& document);

/**
 * @brief Read the content of a project file from a JSON document.
 *
 * Only `version` is required: a member which the document does not hold is
 * read as empty, so a hand written document may list the members it needs.
 * Every member which is present has to fit the schema, and the members of an
 * entry have to be complete.
 *
 * The call is atomic: the document is decoded into a local structure which is
 * assigned to the caller only when every member was accepted, so a failure
 * leaves the caller untouched.
 *
 * @param[in] json Object holding the document.
 * @param[out] document The document to fill.
 * @throw ProjectDocumentError The document does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectDocument& document);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PROJECT_DOCUMENT_HPP
