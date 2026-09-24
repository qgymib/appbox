#ifndef APPBOX_PACKER_CORE_FILESYSTEM_ISOLATION_FILE_HPP
#define APPBOX_PACKER_CORE_FILESYSTEM_ISOLATION_FILE_HPP

#include "FilesystemIsolationModel.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Build the isolation file of a filesystem isolation model.
 *
 * The document carries the isolation modes of the virtual filesystem from the
 * packer to the sandbox (see `common/FilesystemIsolation.hpp` for the schema).
 * Every entry the user set a mode for is listed with its virtual path, its
 * kind and its mode, so the sandbox reads the same modes the workspace shows.
 * An entry which the file does not mention — a path the user never touched, or
 * an entry of a hand written isolation file — still falls back to the closest
 * listed folder above it and to the default of its kind, so the document stays
 * small and a folder which was set to `Full` also covers the entries below it
 * which the model does not even know about.
 *
 * The entries are written in the order of the model, which keeps the document
 * stable for a given model.
 *
 * @param[in] model The filesystem isolation model to describe.
 * @param[out] text The UTF-8 text of the isolation file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildFilesystemIsolationFile(const FilesystemIsolationModel& model, std::string& text, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_FILESYSTEM_ISOLATION_FILE_HPP
