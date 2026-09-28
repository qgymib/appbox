#ifndef APPBOX_TEST_PROBE_CALL_HPP
#define APPBOX_TEST_PROBE_CALL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "loader/Config.hpp"

namespace appbox::test
{

struct ProbeRequest
{
    static constexpr char* Method = "ProbeRequest";

    struct Req
    {
        std::string key;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, key)
    };

    struct Rsp
    {
        std::string    name;
        nlohmann::json data;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, name, data)
    };
};

struct ProbeResponse
{
    static constexpr char* Method = "ProbeResponse";

    struct Req
    {
        std::string    key;
        nlohmann::json result;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, key, result)
    };

    struct Rsp
    {
        int _;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, _)
    };
};

/**
 * @brief Call a probe function.
 * @param[in] name The name of the probe function, encoding in UTF-8.
 * @param[in] data The data to pass to the probe function.
 * @param[in] cwd The current working directory.
 * @param[in] loader_config The loader configuration.
 * @return The result of the probe function.
 */
nlohmann::json ProbeCall(const std::string& name, const nlohmann::json& data, const std::wstring& cwd,
                         const LoaderConfig& loader_config);

/**
 * @brief Startup files the loader started during one run.
 */
struct StartupRun
{
    /**
     * @brief Markers of the startup files which were started, in name order.
     */
    std::vector<std::string> started;

    /**
     * @brief Exit code of the loader.
     */
    std::uint32_t exit_code = 0;
};

/**
 * @brief Run the loader of a configuration and report the startup files it
 *        started.
 *
 * Every startup file of the configuration is started as the probe process of
 * this executable, so every started file reports the marker of its own
 * arguments. The call waits for the loader, which waits for every file it
 * started, so the reported list is complete when the call returns.
 *
 * The executable of a startup file is replaced by this executable, like
 * ProbeCall() does: the paths of a packaged configuration point into the
 * sandbox view, which does not exist on the test machine.
 *
 * @param[in] cwd The current working directory.
 * @param[in] loader_config The loader configuration.
 * @param[in] trigger Trigger passed with `--X-AppBox-Startup`, empty to let
 *                    the loader start the auto start files.
 * @return The markers of the started startup files and the exit code of the
 *         loader.
 */
StartupRun ProbeStartupRun(const std::wstring& cwd, const LoaderConfig& loader_config, const std::string& trigger);

} // namespace appbox::test

#endif
