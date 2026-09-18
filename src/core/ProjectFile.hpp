#ifndef APPBOX_PACKER_CORE_PROJECT_FILE_HPP
#define APPBOX_PACKER_CORE_PROJECT_FILE_HPP

#include "PackModel.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Version of the project file format written by SaveProject().
 *
 * The version is the first member of every project file and is required to
 * match exactly when a file is read, so a file of a future format is rejected
 * instead of being interpreted with the rules of the current one.
 */
inline constexpr int kProjectFileVersion = 1;

/**
 * @brief Write the current packer configuration into a project file.
 *
 * The file is JSON text encoded as strict UTF-8 without a byte order mark:
 * the members of the schema are `version`, `output_path`, `folders`, `files`
 * and, when a main program is selected, `main_program`.
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
 *                     "path": "bin\\app.exe" }
 * }
 * ```
 *
 * Every path is stored as the host path it has on the machine which exported
 * the configuration; the file only records the imports, it never copies the
 * imported content itself. The project file is not the launch configuration
 * of the loader inside a packed archive (`<entry name>.json`), which uses its
 * own schema.
 *
 * An existing file is truncated. A partial file can be left behind when the
 * write fails, in which case the call reports the failure and the caller
 * decides how to react.
 *
 * @param[in] model The configuration to store.
 * @param[in] output_path Destination archive path of the configuration, which
 *                        may be empty when no path was chosen yet.
 * @param[in] path Destination project file path.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool SaveProject(const PackModel& model, const std::wstring& output_path, const std::wstring& path,
                 std::string& error);

/**
 * @brief Replace the packer configuration with the content of a project file.
 *
 * The file has to be JSON text encoded as strict UTF-8; a UTF-16 or UTF-32
 * byte order mark and malformed UTF-8 bytes are rejected with an encoding
 * error instead of being decoded with replacement characters. A leading UTF-8
 * byte order mark is accepted and ignored.
 *
 * The call is atomic: the configuration is decoded and validated into a local
 * model first and is assigned to the caller only when every entry was
 * accepted. A failure therefore leaves both the model and the output path
 * untouched. The source folders and files recorded in the file are not
 * required to exist - a project can be imported on a machine where the
 * packaged application is not installed yet - so only the structure of the
 * file is validated.
 *
 * @param[in] path Project file path.
 * @param[out] model Model replaced with the configuration of the file.
 * @param[out] output_path Destination archive path stored in the file, empty
 *                         when the file records none.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool LoadProject(const std::wstring& path, PackModel& model, std::wstring& output_path,
                 std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PROJECT_FILE_HPP
