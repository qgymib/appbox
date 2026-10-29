#ifndef APPBOX_TEST_UTILS_SELFEXTRACTINGPROGRAM_HPP
#define APPBOX_TEST_UTILS_SELFEXTRACTINGPROGRAM_HPP

#include <filesystem>
#include <string>
#include "src/core/StartupScreen.hpp"

namespace appbox::test
{

/**
 * @brief Name of the program PackProbeSelfExtracting() writes.
 */
inline constexpr const wchar_t* kSelfExtractingProgramName = L"MyApp.exe";

/**
 * @brief Pack a self-extracting program which starts the 64 bit probe.
 *
 * The probe is imported as `MyApp\app.exe`, so the program starts it through the
 * layer of the packaged application. The import folder is removed after the pack
 * run, which leaves the program as the only source of the image.
 *
 * The program carries the configuration and the payload of the session in its
 * own image, so a run of it reads no configuration file beside itself and
 * extracts the resources into a folder of the temporary directory.
 *
 * @param[in] root Directory the program is written into.
 * @param[in] trigger Trigger of the startup file.
 * @param[in] screen Startup screen of the session.
 * @param[out] error Description of the failure.
 * @param[out] sandbox_id Identifier the session packed, may be null. The
 *                         identifier is the one the configuration of the
 *                         program carries, so a case which runs a second
 *                         launcher of the same sandbox names it.
 * @return true on success.
 */
bool PackProbeSelfExtracting(const std::filesystem::path& root, const std::wstring& trigger,
                             const appbox::StartupScreen& screen, std::string& error,
                             std::string* sandbox_id = nullptr);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_SELFEXTRACTINGPROGRAM_HPP
