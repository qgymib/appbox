#ifndef APPBOX_PACKER_CORE_PROJECT_DOCUMENT_HPP
#define APPBOX_PACKER_CORE_PROJECT_DOCUMENT_HPP

#include "EnvironmentIsolation.hpp"
#include "FilesystemIsolation.hpp"
#include "NetworkModel.hpp"
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
 * @brief One startup file of a project document.
 */
struct ProjectStartupRecord
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

    /**
     * @brief Trigger name which selects the startup file.
     */
    std::wstring trigger;

    /**
     * @brief Whether the sandbox starts the file without being asked to.
     */
    bool auto_start = true;
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
 * @brief One DNS redirection of the network part of a project document.
 *
 * The record names a hostname or an IP address the packaged application asks
 * for and the address the name resolves to inside the sandbox, which is the
 * pair of the `Hostname or IP Address` and `Redirect` columns of the network
 * workspace.
 */
struct ProjectDnsRecord
{
    /**
     * @brief Hostname or IP address which is redirected.
     */
    std::wstring hostname;

    /**
     * @brief Address the name resolves to inside the sandbox.
     *
     * The address is an IPv4 or an IPv6 address literal.
     */
    std::wstring redirect;
};

/**
 * @brief The proxy of the network part of a project document.
 *
 * The record describes the SOCKS5 proxy of the packaged application: which
 * protocols it carries, where the server listens and which credentials it
 * expects. It is the counterpart of `NetworkModel::Proxy()` and carries the
 * configuration even while no protocol is enabled, so a proxy the user turned
 * off keeps its server and its credentials in the file.
 */
struct ProjectProxyRecord
{
    /**
     * @brief Protocol of the proxy.
     */
    ProxyType type = ProxyType::Socks5;

    /**
     * @brief Whether the TCP traffic of the application is proxied.
     */
    bool tcp = false;

    /**
     * @brief Whether the UDP traffic of the application is proxied.
     */
    bool udp = false;

    /**
     * @brief Hostname or address of the proxy server.
     */
    std::wstring server;

    /**
     * @brief Port of the proxy server, stored as text like every other scalar.
     */
    std::wstring port;

    /**
     * @brief Optional user name, empty while no authentication is configured.
     */
    std::wstring username;

    /**
     * @brief Optional password, empty while no authentication is configured.
     *
     * The password is stored as plain text: the sandbox has to send it to the
     * proxy server, so the project file cannot hash it.
     */
    std::wstring password;
};

/**
 * @brief One environment variable of the environment part of a project
 *        document.
 *
 * The record names a variable the packaged application sees inside the sandbox,
 * the value the user entered for it and the way that value is composed with the
 * value of the host: the isolation mode decides whether the host value is
 * visible at all, and the merge mode with the merge string decides how the two
 * values are joined while the isolation mode is `WriteCopy`. It is the
 * counterpart of `EnvironmentEntry` of the environment workspace.
 *
 * The record is stored as it is written: the merge mode and the merge string of
 * the search path variable are filled in by the workspace while the variable is
 * entered, so a mode the user picked by hand afterwards is part of the file and
 * survives the next import.
 */
struct ProjectEnvironmentRecord
{
    /**
     * @brief Name of the variable.
     */
    std::wstring name;

    /**
     * @brief Value the user entered, which may be empty.
     */
    std::wstring value;

    /**
     * @brief Isolation mode of the variable.
     */
    EnvironmentIsolation isolation = EnvironmentIsolation::WriteCopy;

    /**
     * @brief Merge mode of the variable.
     */
    EnvironmentMergeMode merge = environment_isolation::kDefaultMergeMode;

    /**
     * @brief Text which joins the two values while the merge mode is `Prepend`
     *        or `Append`, empty while the user entered none.
     */
    std::wstring merge_string;
};

/**
 * @brief The content of a project file.
 *
 * The structure is the document of the `File -> Export Configuration...` and
 * `File -> Import Configuration...` commands: the imported folders and files,
 * the startup files, the virtual registry, the isolation modes of the virtual
 * filesystem, the DNS redirections of the network workspace, the proxy of the
 * network workspace and the environment variables of the environment
 * workspace. It holds no host state and no wxWidgets dependency, so the
 * conversion is unit testable.
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
 *   "startup_files": [ { "preset": "program_files", "folder": "MyApp",
 *                        "path": "bin\\app.exe", "trigger": "app",
 *                        "auto_start": true } ],
 *   "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "full",
 *                   "values": [ { "name": "Server", "type": "REG_SZ",
 *                                 "data": "68 00 65 00 6C 00 6C 00 6F 00",
 *                                 "isolation": "write_copy" } ],
 *                   "children": [] } ],
 *   "filesystem": [ { "path": "#ProgramFiles#\\MyApp", "kind": "directory",
 *                     "isolation": "whiteout" } ],
 *   "network": [ { "hostname": "update.example.com",
 *                  "redirect": "127.0.0.1" } ],
 *   "proxy": { "type": "socks5", "tcp": true, "udp": false,
 *              "server": "127.0.0.1", "port": "1080",
 *              "username": "user", "password": "secret" },
 *   "environment": [ { "name": "PATH", "value": "C:\\MyApp\\bin",
 *                      "isolation": "write_copy", "merge": "prepend",
 *                      "merge_string": ";" } ]
 * }
 * ```
 *
 * Every path is stored as the host path it has on the machine which exported
 * the configuration; the file only records the imports, it never copies the
 * imported content itself.
 *
 * The `proxy` member is written only while a proxy is configured, so a
 * document of a session without a proxy keeps the text it had before the
 * member was added to the schema.
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
     * @brief The startup files of the packaged application, in startup order.
     */
    std::vector<ProjectStartupRecord> startup_files;

    /**
     * @brief The root keys of the virtual registry with their subtree.
     */
    std::vector<ProjectRegistryKeyRecord> registry;

    /**
     * @brief The isolation modes of the virtual filesystem.
     */
    std::vector<ProjectFilesystemRecord> filesystem;

    /**
     * @brief The DNS redirections of the network workspace.
     */
    std::vector<ProjectDnsRecord> network;

    /**
     * @brief The proxy of the network workspace, absent while none is
     *        configured.
     *
     * A proxy counts as configured as soon as one of its protocols is enabled
     * or one of its fields carries a value, so a configuration which was typed
     * and then disabled is part of the document as well.
     */
    std::optional<ProjectProxyRecord> proxy;

    /**
     * @brief The environment variables of the environment workspace.
     */
    std::vector<ProjectEnvironmentRecord> environment;
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
 * @brief Store one startup file of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectStartupRecord& record);

/**
 * @brief Read one startup file of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectStartupRecord& record);

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
 * @brief Store one DNS redirection of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectDnsRecord& record);

/**
 * @brief Read one DNS redirection of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectDnsRecord& record);

/**
 * @brief Store the proxy of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectProxyRecord& record);

/**
 * @brief Read the proxy of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectProxyRecord& record);

/**
 * @brief Store one environment variable of a document.
 * @param[out] json Object which receives the record.
 * @param[in] record The record to store.
 */
void to_json(nlohmann::ordered_json& json, const ProjectEnvironmentRecord& record);

/**
 * @brief Read one environment variable of a document.
 * @param[in] json Object holding the record.
 * @param[out] record The record to fill.
 * @throw ProjectDocumentError The object does not fit the schema.
 */
void from_json(const nlohmann::ordered_json& json, ProjectEnvironmentRecord& record);

/**
 * @brief Store the content of a project file as a JSON document.
 *
 * The members are written in the order of ProjectDocument with the schema
 * version first, so the text of a given document is stable and easy to read.
 * Every member is written, except `proxy`, which is omitted while no proxy is
 * configured.
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
