#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "utils/WinAPI.h"
#include "utils/PipeClient.hpp"
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
