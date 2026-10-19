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
     *
     * The probe executable defines the object of a probe with the function
     * which implements it, so the constructor registers the function. The test
     * executable defines the same object without a function: it only carries
     * the name, because the work runs in the probe process, and a null function
     * registers nothing.
     *
     * @param[in] name The name of the probe function.
     * @param[in] fn The probe function, null in the test executable.
     */
    Probe(const std::string& name, Fn fn = nullptr)
    {
        this->name = name;
        if (fn != nullptr)
        {
            GetMap()[name] = fn;
        }
    }

    /**
     * @brief Call the probe function.
     * The probe function is called in subprocess hooked by sandbox.
     * @param[in] data The data to be passed to the probe function.
     * @param[in] cwd The current working directory.
     * @param[in] launcher_config The launcher configuration.
     * @param[in] bitness The bit size of the probe process which runs the
     *                    function, see ProbeBitness.
     * @return The result of the probe function.
     */
    nlohmann::json Call(const nlohmann::json& data, const std::wstring& cwd, const LauncherConfig& launcher_config,
                        ProbeBitness bitness = ProbeBitness::Bit64)
    {
        return ProbeCall(name, data, cwd, launcher_config, LogRun{}, bitness);
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
