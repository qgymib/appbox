#ifndef APPBOX_LOADER_CONFIG_HPP
#define APPBOX_LOADER_CONFIG_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox
{

struct LoaderLaunch
{
    /**
     * @brief Executable path.
     */
    std::string executable;

    /**
     * @brief Executable arguments.
     */
    std::vector<std::string> arguments;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoaderLaunch, executable, arguments)
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
     * @brief Launch configuration.
     */
    LoaderLaunch launch;

    /**
     * @brief Environment variables.
     */
    std::vector<LoaderEnvironment> environment;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoaderConfig, enable_admin_ui, launch, environment)
};

} // namespace appbox

#endif
