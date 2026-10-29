#include "WinAPI.h" /* Must be first include file */
#include <CLI/CLI.hpp>
#include "ParamCheck.hpp"
#include "common/SandboxLayout.hpp"
#include "utils/GetPEB.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Drop the separators a directory path may end with.
 *
 * The working directory of a process is reported with a trailing separator
 * while a case spells it without one, and both name the same directory, so the
 * comparison ignores the difference. The root of a drive keeps its separator.
 *
 * @param[in] path The path to trim.
 * @return The path without the separators it ends with.
 */
std::wstring TrimTrailingSeparators(std::wstring path)
{
    while (path.size() > 3 && path.back() == L'\\')
    {
        path.pop_back();
    }

    return path;
}

/**
 * @brief Compare the parameters of this process with the values of the case.
 *
 * The worker never returns: it leaves the process through the exit code of the
 * observations, which the case reads in the process which started it.
 *
 * @param[in] expected_image Image path the case expects, empty to skip the
 *                           observation.
 * @param[in] expected_directory Working directory the case expects, empty to
 *                               skip the observation.
 */
void RunParamCheck(const std::string& expected_image, const std::string& expected_directory)
{
    unsigned long bits = 0;

    if (!expected_image.empty() && appbox::GetImagePathFromPeb() == appbox::UTF8ToWide(expected_image))
    {
        bits |= appbox::test::ParamCheckImageMatched;
    }

    if (!expected_directory.empty() && TrimTrailingSeparators(appbox::GetCurrentDirectoryFromPeb()) ==
                                           TrimTrailingSeparators(appbox::UTF8ToWide(expected_directory)))
    {
        bits |= appbox::test::ParamCheckCurrentDirectoryMatched;
    }

    if (GetModuleHandleW(appbox::layout::kSandbox64DllNameW) != nullptr ||
        GetModuleHandleW(appbox::layout::kSandbox32DllNameW) != nullptr)
    {
        bits |= appbox::test::ParamCheckSandboxLoaded;
    }

    throw CLI::RuntimeError(static_cast<int>(bits));
}

} // namespace

void appbox::test::ParamCheckInit(CLI::App& app)
{
    /*
     * The options are static, because the callback of the subcommand runs after
     * the parse of the command line and the subcommand is registered once for
     * the whole life of the process.
     */
    static std::string expected_image;
    static std::string expected_directory;

    auto cmd = app.add_subcommand("paramcheck", "Worker of a case: report the process parameters of the view");
    cmd->add_option("--image", expected_image, "Image path the process has to report, encoding in UTF-8");
    cmd->add_option("--cwd", expected_directory, "Working directory the process has to report, encoding in UTF-8");
    cmd->final_callback([&]() { RunParamCheck(expected_image, expected_directory); });
}
