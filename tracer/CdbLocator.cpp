#include "tracer/CdbLocator.hpp"
#include "WString.hpp"
#include <windows.h>
#include <string>

namespace appbox::tracer
{
namespace
{

/**
 * @brief Read an environment variable as a wide string.
 *
 * @param[in] name Name of the variable.
 * @return The value, or an empty string when the variable is not set.
 */
std::wstring EnvironmentValue(const wchar_t* name)
{
    const DWORD required = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
    {
        return {};
    }

    std::wstring value(required, L'\0');
    const DWORD written = ::GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required)
    {
        return {};
    }

    value.resize(written);
    return value;
}

/**
 * @brief Append `<directory>\cdb.exe` when the directory is not empty.
 *
 * @param[in,out] candidates Candidate list to extend.
 * @param[in] directory Directory to test.
 */
void AppendCandidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& directory)
{
    if (!directory.empty())
    {
        candidates.push_back(directory / kCdbFileName);
    }
}

/**
 * @brief Append the debugger directories below one Windows Kits root.
 *
 * The version directory `10` is probed first because it is the layout a default
 * install uses; afterwards every other version directory which carries a
 * `Debuggers` tree is added, so an older or newer kit is found as well.
 *
 * @param[in,out] candidates Candidate list to extend.
 * @param[in] kits_root Directory which holds the version directories.
 */
void AppendKitsCandidates(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& kits_root)
{
    if (kits_root.empty())
    {
        return;
    }

    AppendCandidate(candidates, kits_root / L"10" / L"Debuggers" / L"x64");
    AppendCandidate(candidates, kits_root / L"10" / L"Debuggers" / L"x86");

    std::error_code error;
    std::filesystem::directory_iterator iterator(kits_root, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry entry = *iterator;
        if (entry.is_directory(error))
        {
            AppendCandidate(candidates, entry.path() / L"Debuggers" / L"x64");
            AppendCandidate(candidates, entry.path() / L"Debuggers" / L"x86");
        }

        error.clear();
        iterator.increment(error);
    }
}

} // namespace

std::vector<std::filesystem::path> DefaultCdbCandidates()
{
    std::vector<std::filesystem::path> candidates;

    /* A debugger which is on the PATH wins: it is the one the user picked. */
    for (const auto& directory : appbox::Split(EnvironmentValue(L"PATH"), L";"))
    {
        AppendCandidate(candidates, std::filesystem::path(directory));
    }

    /* The debuggers of the Windows SDK live below the Windows Kits directory. */
    const std::wstring program_files_x86 = EnvironmentValue(L"ProgramFiles(x86)");
    if (!program_files_x86.empty())
    {
        AppendKitsCandidates(candidates, std::filesystem::path(program_files_x86) / L"Windows Kits");
    }

    const std::wstring program_files = EnvironmentValue(L"ProgramFiles");
    if (!program_files.empty())
    {
        AppendKitsCandidates(candidates, std::filesystem::path(program_files) / L"Windows Kits");
    }

    return candidates;
}

std::filesystem::path FindCdb(const std::vector<std::filesystem::path>& candidates)
{
    for (const auto& candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error)
        {
            return candidate;
        }
    }

    return {};
}

std::filesystem::path ResolveCdb(const std::filesystem::path& explicit_path)
{
    if (!explicit_path.empty())
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(explicit_path, error) && !error)
        {
            return explicit_path;
        }

        return {};
    }

    return FindCdb(DefaultCdbCandidates());
}

} // namespace appbox::tracer
