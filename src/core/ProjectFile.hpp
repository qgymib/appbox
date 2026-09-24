#ifndef APPBOX_PACKER_CORE_PROJECT_FILE_HPP
#define APPBOX_PACKER_CORE_PROJECT_FILE_HPP

#include "FilesystemIsolationModel.hpp"
#include "PackModel.hpp"
#include "ProjectDocument.hpp"
#include "RegistryModel.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Build the document of a project file from the models of a session.
 *
 * The document describes the current configuration: the imported folders, the
 * imported files, the main program, the virtual registry with the isolation
 * modes of every key and value, the isolation modes of the virtual filesystem
 * and the path of the `Output File` box. The structure and the schema of the
 * document are described by ProjectDocument.
 *
 * The imported folders are listed preset directory by preset directory, so the
 * order of the document does not depend on the order the user imported them
 * in.
 *
 * @param[in] model Configuration of the packer session.
 * @param[in] registry Virtual registry of the workspace.
 * @param[in] isolation Isolation modes of the virtual filesystem.
 * @param[in] output_path Destination archive path of the configuration, which
 *                        may be empty when no path was chosen yet.
 * @return The document of the session.
 */
ProjectDocument MakeProjectDocument(const PackModel& model, const RegistryModel& registry,
                                    const FilesystemIsolationModel& isolation, const std::wstring& output_path);

/**
 * @brief Replace the models of a session with the content of a document.
 *
 * Every entry of the document is validated by the model it belongs to, so a
 * document which names an unknown preset directory, a file outside an imported
 * folder, an unknown root key or a mode a file cannot hold is rejected with the
 * description of the model.
 *
 * The call is atomic: the entries are applied to local models which replace the
 * caller only when every entry was accepted, so a failure leaves all models and
 * the output path untouched. The source folders and files recorded in the
 * document are not required to exist - a project can be imported on a machine
 * where the packaged application is not installed yet - so only the structure
 * of the document is validated.
 *
 * @param[in] document Document to apply.
 * @param[out] model Model replaced with the configuration of the document.
 * @param[out] registry Registry replaced with the registry of the document.
 * @param[out] isolation Isolation modes replaced with the modes of the document.
 * @param[out] output_path Destination archive path of the document.
 * @param[out] error Error description on failure, prefixed with the path of the
 *                   entry which was rejected, for example `folders[1]: ...`.
 * @return true on success.
 */
bool ApplyProjectDocument(const ProjectDocument& document, PackModel& model, RegistryModel& registry,
                          FilesystemIsolationModel& isolation, std::wstring& output_path, std::string& error);

/**
 * @brief Write the content of a document into a project file.
 *
 * The file is JSON text encoded as strict UTF-8 without a byte order mark; the
 * members are written in the order of the schema with the version first, so the
 * text of a given document is stable and easy to read and diff.
 *
 * An existing file is truncated. A partial file can be left behind when the
 * write fails, in which case the call reports the failure and the caller
 * decides how to react.
 *
 * @param[in] document The document to store.
 * @param[in] path Destination project file path.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool SaveProject(const ProjectDocument& document, const std::wstring& path, std::string& error);

/**
 * @brief Read the content of a project file into a document.
 *
 * The file has to be JSON text encoded as strict UTF-8; a UTF-16 or UTF-32
 * byte order mark and malformed UTF-8 bytes are rejected with an encoding
 * error instead of being decoded with replacement characters. A leading UTF-8
 * byte order mark is accepted and ignored.
 *
 * The call is atomic: the document is decoded into a local structure which
 * replaces the caller only when the whole file was accepted, so a failure
 * leaves the document untouched.
 *
 * @param[in] path Project file path.
 * @param[out] document Document replaced with the content of the file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool LoadProject(const std::wstring& path, ProjectDocument& document, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PROJECT_FILE_HPP
