#ifndef APPBOX_LOADER_CONFIG_HPP
#define APPBOX_LOADER_CONFIG_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One executable the loader starts.
 *
 * A startup file with `auto_start` set is started as soon as the loader runs.
 * A startup file without the flag is started only when the loader is asked for
 * its `trigger` through the `--X-AppBox-Startup` option.
 */
struct LoaderStartup
{
    /**
     * @brief Trigger which selects the startup file.
     */
    std::string trigger;

    /**
     * @brief Whether the loader starts the file without being asked to.
     */
    bool auto_start = true;

    /**
     * @brief Executable path.
     */
    std::string executable;

    /**
     * @brief Executable arguments.
     */
    std::vector<std::string> arguments;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoaderStartup, trigger, auto_start, executable, arguments)
};

struct LoaderEnvironment
{
    /**
     * @brief Environment variable key.
     */
    std::string key;

    /**
     * @brief Environment variable value.
     */
    std::string value;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoaderEnvironment, key, value)
};

struct LoaderConfig
{
    /**
     * @brief Enable admin UI.
     */
    bool enable_admin_ui = false;

    /**
     * @brief Base filesystem.
     */
    std::vector<std::string> base_fs;

    /**
     * @brief overlay filesystem.
     */
    std::string overlay_fs = "data";

    /**
     * @brief Startup files, in the order the loader starts them.
     */
    std::vector<LoaderStartup> startups;

    /**
     * @brief Environment variables.
     */
    std::vector<LoaderEnvironment> environment;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoaderConfig, enable_admin_ui, base_fs, overlay_fs, startups,
                                                environment)
};

} // namespace appbox

#endif
