#ifndef APPBOX_TEST_PROBE_INIT_HPP
#define APPBOX_TEST_PROBE_INIT_HPP

#include <CLI/CLI.hpp>
#include <map>
#include "utils/ProbeCall.hpp"

namespace appbox::test
{

/**
 * @brief Probe node.
 * It must be global variable so that it can be registered on application start.
 */
struct Probe
{
    typedef nlohmann::json (*Fn)(const nlohmann::json& data);
    typedef std::map<std::string, Fn> Map;

    /**
     * @brief Register a probe function.
     * @param[in] name The name of the probe function.
     * @param[in] fn The probe function.
     */
    Probe(const std::string& name, Fn fn)
    {
        this->name = name;
        GetMap()[name] = fn;
    }

    /**
     * @brief Call the probe function.
     * The probe function is called in subprocess hooked by sandbox.
     * @param[in] data The data to be passed to the probe function.
     * @param[in] cwd The current working directory.
     * @param[in] launcher_config The launcher configuration.
     * @return The result of the probe function.
     */
    nlohmann::json Call(const nlohmann::json& data, const std::wstring& cwd, const LauncherConfig& launcher_config)
    {
        return ProbeCall(name, data, cwd, launcher_config);
    }

    static Map& GetMap()
    {
        static Map probe_map;
        return probe_map;
    }

    std::string name;
};

/**
 * @brief Initialize the probe module and register the probe command.
 * @param[in,out] app The CLI app.
 */
void ProbeInit(CLI::App& app);

/**
 * @brief Get the startup marker given on the command line of this process.
 *
 * The end-to-end cases of the launcher put the marker of a startup file into the
 * arguments of that file, so the probe process can tell which startup file the
 * launcher started it for.
 *
 * @return The marker, empty when the process was not started as a startup
 *         file.
 */
const std::string& StartupMarker();

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_INIT_HPP
