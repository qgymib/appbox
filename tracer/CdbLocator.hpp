#ifndef APPBOX_TRACER_CDBLOCATOR_HPP
#define APPBOX_TRACER_CDBLOCATOR_HPP

#include <filesystem>
#include <vector>

namespace appbox::tracer
{

/** File name of the debugger the tracer drives. */
inline constexpr const wchar_t* kCdbFileName = L"cdb.exe";

/**
 * @brief Build the candidate list of debugger locations.
 *
 * The candidates are derived from the environment, so the search works on a
 * machine where the debugger was installed by the Windows SDK and on a machine
 * where cdb.exe is on the PATH. The list is returned as data instead of being
 * searched internally, which keeps the search order testable.
 *
 * Order: every directory of PATH, then the debugger directories below the
 * Windows Kits installation roots (x64 before x86).
 *
 * @return Candidate paths, most specific first; the entries are not checked.
 */
std::vector<std::filesystem::path> DefaultCdbCandidates();

/**
 * @brief Return the first candidate which exists.
 *
 * @param[in] candidates Candidate paths, checked in order.
 * @return The first existing candidate, or an empty path when none exists.
 */
std::filesystem::path FindCdb(const std::vector<std::filesystem::path>& candidates);

/**
 * @brief Resolve the debugger to use.
 *
 * An explicit path wins and is returned only when it exists; otherwise the
 * default candidate list is searched.
 *
 * @param[in] explicit_path Path given with `--cdb`, empty when it was omitted.
 * @return The debugger path, or an empty path when it could not be found.
 */
std::filesystem::path ResolveCdb(const std::filesystem::path& explicit_path);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_CDBLOCATOR_HPP
