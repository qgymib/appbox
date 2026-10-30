#ifndef APPBOX_TEST_PROBE_INIT_HPP
#define APPBOX_TEST_PROBE_INIT_HPP

#include <CLI/CLI.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief The probes the probe executable serves.
 *
 * The list is the only place which names a probe: it is expanded into the table
 * of registrars of `test/probe/Methods.cpp`, so a probe which is added to the
 * tree is added here, and a probe which is missing here fails to link.
 */
/* clang-format off */
#define APPBOX_PROBE_METHODS(xx) \
    xx(ConsoleWindow)            \
    xx(CreateDirectoryW)         \
    xx(CreateFileW)              \
    xx(DeleteFileW)              \
    xx(EnvironmentRead)          \
    xx(EnvironmentWrite)         \
    xx(Exit)                     \
    xx(FindStreams)              \
    xx(Fonts)                    \
    xx(Job)                      \
    xx(LaunchProcess)            \
    xx(ListDir)                  \
    xx(ListDirNt)                \
    xx(LoadedModules)            \
    xx(QueryAttributes)          \
    xx(QueryFullAttributes)      \
    xx(QueryInformationByName)   \
    xx(QueryInformationFile)     \
    xx(ReadFileFull)             \
    xx(RegDeleteKey)             \
    xx(RegDeleteValue)           \
    xx(RegEnumKey)               \
    xx(RegEnumValue)             \
    xx(RegKeyApi)                \
    xx(RegOpenWriteValue)        \
    xx(RegQueryKeyName)          \
    xx(RegQueryMultipleValues)   \
    xx(RegReadValue)             \
    xx(RegReadValues)            \
    xx(RegRenameKey)             \
    xx(RegSaveKey)               \
    xx(RegShadowRead)            \
    xx(RegTransacted)            \
    xx(RegTwoProcesses)          \
    xx(RegWriteValue)            \
    xx(RemoveDirectory)          \
    xx(ReparsePoint)             \
    xx(ResolveName)              \
    xx(SetInformationFile)       \
    xx(SocketTraffic)            \
    xx(StartupStarted)           \
    xx(WriteFile)
/* clang-format on */

/**
 * @brief Error code of a request whose parameters cannot be read.
 *
 * The code is the one of the JSON-RPC error object, so a caller of a probe can
 * tell a request the probe could not read from an answer it could not decode.
 */
constexpr int kInvalidParamsCode = -32602;

/**
 * @brief Handler of one probe: answers the parameters of a request.
 *
 * The handler is what the probe runtime registers on its session, so the
 * runtime knows no probe of its own and the session type stays out of the
 * headers of the probes.
 */
typedef std::function<nlohmann::json(const nlohmann::json& params)> ProbeHandler;

/**
 * @brief Register one probe under the method of its protocol.
 *
 * The probe executable calls it for every probe of `APPBOX_PROBE_METHODS` once,
 * before it serves anything.
 *
 * @param[in] method Method of the probe, which is the name of its protocol.
 * @param[in] handler Handler of the probe.
 */
void RegisterProbeMethod(const std::string& method, ProbeHandler handler);

/**
 * @brief Register every probe of the probe executable.
 *
 * The call expands `APPBOX_PROBE_METHODS` into one registration per probe, so a
 * probe which is added to the tree is added in one place only.
 */
void RegisterProbes();

/**
 * @brief Adapt the typed entry of a probe to the JSON dispatch of the runtime.
 *
 * @tparam Protocol Protocol struct of the probe, see `test/probe/<Name>.hpp`.
 * @param[in] entry Implementation of the probe.
 */
template <typename Protocol>
void RegisterProbe(typename Protocol::Rsp (*entry)(const typename Protocol::Req&))
{
    RegisterProbeMethod(Protocol::Method, [entry](const nlohmann::json& params) -> nlohmann::json {
        return entry(params.get<typename Protocol::Req>());
    });
}

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
