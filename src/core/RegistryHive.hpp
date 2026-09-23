#ifndef APPBOX_PACKER_CORE_REGISTRY_HIVE_HPP
#define APPBOX_PACKER_CORE_REGISTRY_HIVE_HPP

#include "RegistryModel.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Write the virtual registry of a model into a hive file.
 *
 * The hive is a real registry file: it is created by mounting a fresh file
 * with `RegLoadAppKeyW`, filled with the content of the model and flushed and
 * closed again, which writes it back to disk. The sandbox mounts the very same
 * file as its private application hive, so the packed registry is exactly the
 * registry the sandboxed process sees.
 *
 * The hive holds one sub key per root key of the model (`HKEY_LOCAL_MACHINE`,
 * `HKEY_CURRENT_USER`, ...), so the path of an entry inside the hive equals its
 * path in the model. Every value is written with the raw bytes and the `REG_*`
 * type code of the model, so all supported types — including `REG_NONE` —
 * survive the round trip.
 *
 * An existing file at the destination is replaced. A failure removes the
 * partially written file, so the caller never sees a half written hive.
 *
 * @param[in] model The registry model to store.
 * @param[in] path Destination path of the hive file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteRegistryHive(const RegistryModel& model, const std::wstring& path, std::string& error);

/**
 * @brief Build the hive of a model in memory.
 *
 * The hive is written to a temporary file and read back, because the registry
 * API only works on files. The temporary file is removed before the call
 * returns, also on failure.
 *
 * @param[in] model The registry model to store.
 * @param[out] bytes The bytes of the hive file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildRegistryHiveBytes(const RegistryModel& model, std::vector<std::uint8_t>& bytes, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_REGISTRY_HIVE_HPP
