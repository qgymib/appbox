#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "utils/WinAPI.h"
#include "utils/PipeClient.hpp"
#include "filesystem/IsolationTable.hpp"
#include "filesystem/Resolve.hpp"
#include "Config.hpp"
#include <nlohmann/json.hpp>

namespace appbox
{

struct Sandbox
{
    /**
     * @brief Sandbox isolation enable flag.
     */
    bool bIsolationMode = false;

    std::string inject_data;

    /**
     * @brief Named pipe path.
     */
    std::wstring wPipePath;

    /**
     * @brief Filesystem configuration.
     */
    filesystem::ResolveFs fs;

    /**
     * @brief Isolation modes of the virtual filesystem.
     *
     * The table is filled by the filesystem isolation module from the
     * isolation file of the injected configuration. An empty table means that
     * no mode was configured, so every entry keeps the default mode of its
     * kind and the host filesystem stays visible.
     */
    filesystem::IsolationTable fs_isolation;

    /**
     * @brief Registry hive file path (DOS style).
     *
     * Empty when registry isolation is not configured.
     */
    std::wstring wRegistryHiveDOSPath;

    /**
     * @brief Registry isolation file path (DOS style).
     *
     * The file carries the isolation modes of the virtual registry. An empty
     * path or a missing file means that no entry was configured, so every
     * entry keeps the default mode `Write Copy` and the host registry stays
     * visible.
     */
    std::wstring wRegistryIsolationDOSPath;

    /**
     * @brief Filesystem isolation file path (DOS style).
     *
     * The file carries the isolation modes of the virtual filesystem. An empty
     * path or a missing file means that no entry was configured, so every
     * entry keeps the default mode of its kind (`Write Copy` for a folder,
     * `Full` for a file) and the host filesystem stays visible.
     */
    std::wstring wFilesystemIsolationDOSPath;

    /**
     * @brief Path to 32-bit sandbox dll path. Encoding in UTF-8.
     */
    std::string sandbox32_dos_path;

    /**
     * @brief Path to 64-bit sandbox dll path. Encoding in UTF-8.
     */
    std::string sandbox64_dos_path;

    /**
     * @brief RPC client
     */
    std::shared_ptr<appbox::PipeClient> client;
};
void to_json(nlohmann::json& j, const Sandbox& r);

/**
 * @brief Global sandbox instance
 */
extern Sandbox* sandbox;

} // namespace appbox

#endif
