#ifndef APPBOX_PACKER_CORE_REGISTRY_ISOLATION_FILE_HPP
#define APPBOX_PACKER_CORE_REGISTRY_ISOLATION_FILE_HPP

#include "RegistryModel.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Build the isolation file of a registry model.
 *
 * The document carries the isolation modes of the virtual registry from the
 * packer to the sandbox (see `common/RegistryIsolation.hpp` for the schema).
 * Every key and every value of the model is listed with the explicit mode it
 * holds, so the sandbox reads the same modes the workspace shows. An entry
 * which the file does not mention — a key the sandboxed process creates, or an
 * entry of a hand written isolation file — still falls back to the closest
 * listed key above it, so a key which was set to `Full` also covers the host
 * entries below it which the model does not even know about.
 *
 * The keys and values are written in the order of the model, which keeps the
 * document stable for a given model.
 *
 * @param[in] model The registry model to describe.
 * @param[out] text The UTF-8 text of the isolation file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildRegistryIsolationFile(const RegistryModel& model, std::string& text, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_REGISTRY_ISOLATION_FILE_HPP
