#ifndef APPBOX_PACKER_CORE_ENVIRONMENT_ISOLATION_FILE_HPP
#define APPBOX_PACKER_CORE_ENVIRONMENT_ISOLATION_FILE_HPP

#include "EnvironmentModel.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Build the environment isolation file of the environment workspace.
 *
 * The document carries the environment variables of the workspace from the
 * packer to the sandbox (see `common/EnvironmentIsolation.hpp` for the
 * schema): every variable is listed with the value the user entered, its
 * isolation mode, its merge mode and the text which joins the two values. The
 * packer writes the file into the environment domain of the archive as
 * `app/environment/isolation.json`, the launcher hands its path to the sandbox,
 * and the sandbox composes the environment of the packaged application from
 * the entries while it starts.
 *
 * The entries are written in the order of the model, which keeps the document
 * stable for a given model, and every member of an entry is written, so a
 * reader never has to guess what a missing member means. A session without a
 * variable writes the document of an empty entry list.
 *
 * The search path rule of the workspace is not applied here: the model stores
 * the modes the user picked, and the workspace fills them in while a name
 * becomes the search path variable. A mode the user picked by hand is
 * therefore carried into the file unchanged.
 *
 * @param[in] model The environment model to describe.
 * @param[out] text The UTF-8 text of the isolation file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildEnvironmentIsolationFile(const EnvironmentModel& model, std::string& text, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ENVIRONMENT_ISOLATION_FILE_HPP
