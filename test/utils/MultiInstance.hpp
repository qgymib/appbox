#ifndef APPBOX_TEST_UTILS_MULTIINSTANCE_HPP
#define APPBOX_TEST_UTILS_MULTIINSTANCE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "launcher/Config.hpp"

namespace appbox::test
{

/**
 * @brief Name of the configuration file of a second launcher instance.
 *
 * Two launchers of one sandbox share the directory which carries the sandbox,
 * because the root of the sandbox is the directory of the configuration. The
 * second instance therefore writes its configuration beside the file of the
 * master instance instead of over it.
 */
inline constexpr const wchar_t* kSecondaryConfigName = L"config.secondary.json";

/**
 * @brief Time the programs of a master instance stay alive, in milliseconds.
 *
 * The run of a launcher ends with its last program, so a case which observes
 * the sandbox of a master instance while it works needs programs which outlive
 * the observation.
 */
inline constexpr std::uint32_t kMasterHoldMs = 2000;

/**
 * @brief Build the configuration of a sandbox which starts one probe process.
 *
 * The configuration carries the layout a packed archive uses: the state below
 * `data` and the read-only resources below `app`, with one layer for the
 * profile folder of the user.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @param[in] sandbox_id Identifier the run has to use.
 * @param[in] marker Marker of the startup file, which the probe reports back.
 * @return The configuration with one auto start file.
 */
appbox::LauncherConfig MultiInstanceConfig(const std::filesystem::path& root, const std::string& sandbox_id,
                                           const std::string& marker);

/**
 * @brief Build the document of a second launcher instance.
 *
 * The document names the identifier of the sandbox and nothing else: a second
 * instance starts no program of its own, and the master instance decides what
 * the request runs.
 *
 * @param[in] sandbox_id Identifier of the sandbox.
 * @return The configuration document of the second instance.
 */
nlohmann::json MultiInstanceRequestDocument(const std::string& sandbox_id);

/**
 * @brief Wait until the control pipe of a sandbox exists.
 *
 * The pipe exists from the moment the launcher which won the race created its
 * first instance, which is before that launcher prepared the sandbox. A case
 * which waits for it therefore sends its request while the master instance is
 * still preparing.
 *
 * @param[in] sandbox_id Identifier of the sandbox.
 * @param[in] timeout_ms Budget of the wait.
 * @return true when the pipe exists, false when the budget was used up.
 */
bool WaitForControlPipe(const std::string& sandbox_id, int timeout_ms);

/**
 * @brief Read the log files a run wrote into the log directory of a case.
 * @param[in] root Working directory of the case.
 * @return The content of every log file, empty when the run wrote none.
 */
std::string ReadRunLog(const std::filesystem::path& root);

/**
 * @brief Wait until the log files of a case carry a text.
 *
 * The call is the way a case observes a stage of a run which is still working:
 * the launcher reports the stage in its own log file, which the case reads
 * while the run goes on.
 *
 * @param[in] root Working directory of the case.
 * @param[in] text Text the log files have to carry.
 * @param[in] timeout_ms Budget of the wait.
 * @return true when the text appeared, false when the budget was used up.
 */
bool WaitForRunLog(const std::filesystem::path& root, const std::string& text, int timeout_ms);

/**
 * @brief Count the log files a case holds.
 * @param[in] root Working directory of the case.
 * @return The number of log files in the log directory of the case.
 */
std::size_t LogFileCount(const std::filesystem::path& root);

/**
 * @brief Describe the entries of a directory tree.
 *
 * Every entry is reported with its path, its size and the time it was written,
 * so two descriptions which are equal prove that a run neither added an entry
 * nor changed one.
 *
 * @param[in] dir Directory to describe, which does not have to exist.
 * @return One text per entry, in path order.
 */
std::vector<std::string> TreeSnapshot(const std::filesystem::path& dir);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_MULTIINSTANCE_HPP
