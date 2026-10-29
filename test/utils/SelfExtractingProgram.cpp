#include "utils/LauncherPath.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "src/core/PackService.hpp"
#include "SelfExtractingProgram.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Read the payload of the launcher of the suite.
 * @return The bytes of the launcher, empty when the run named no launcher.
 */
std::vector<char> ReadLauncherPayload()
{
    const auto& path = appbox::test::LauncherPath();
    if (path.empty())
    {
        return {};
    }

    std::string data;
    if (appbox::test::ReadFileFull(path, data) != 0)
    {
        return {};
    }

    return std::vector<char>(data.begin(), data.end());
}

} // namespace

bool appbox::test::PackProbeSelfExtracting(const std::filesystem::path& root, const std::wstring& trigger,
                                           const appbox::StartupScreen& screen, std::string& error,
                                           std::string* sandbox_id)
{
    const auto& probe = ProbePath(ProbeBitness::Bit64);
    if (probe.empty())
    {
        error = "the run was started without the path of the 64 bit probe";
        return false;
    }

    const auto import = root / L"import";
    const auto application = import / L"MyApp";

    std::error_code ec;
    std::filesystem::create_directories(application, ec);
    if (ec)
    {
        error = "failed to create the import folder: " + ec.message();
        return false;
    }

    std::filesystem::copy_file(probe, application / L"app.exe", std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        error = "failed to copy the probe: " + ec.message();
        return false;
    }

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    if (!model.ImportFolder("program_files", application.wstring(), error))
    {
        return false;
    }
    if (!model.RestoreStartupFile("program_files", L"MyApp", L"app.exe", trigger, true, error))
    {
        return false;
    }

    /*
     * The identifier of the session is the one the configuration of the program
     * carries, so the caller learns it before the program is packed.
     */
    const auto id = model.EnsureSandboxId();

    const auto payload = ReadLauncherPayload();
    if (payload.empty())
    {
        error = "failed to read the launcher payload";
        return false;
    }

    appbox::PackPayloads payloads;
    payloads.launcher_bytes = payload.data();
    payloads.launcher_size = payload.size();

    const auto exe_path = root / kSelfExtractingProgramName;
    const auto result = appbox::PackSelfExtracting(
        model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(), appbox::EnvironmentModel(),
        appbox::ApplicationMetadata(), screen, payloads, appbox::CompressionLevel::Normal, exe_path.wstring(), nullptr);
    if (!result.empty())
    {
        error = result;
        return false;
    }

    std::filesystem::remove_all(import, ec);

    if (sandbox_id != nullptr)
    {
        *sandbox_id = id;
    }

    return true;
}
