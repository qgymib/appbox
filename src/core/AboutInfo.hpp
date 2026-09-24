#ifndef APPBOX_PACKER_CORE_ABOUT_INFO_HPP
#define APPBOX_PACKER_CORE_ABOUT_INFO_HPP

#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One third-party library the binary was linked against.
 */
struct DependencyInfo
{
    /**
     * @brief Name of the library.
     */
    std::string name;

    /**
     * @brief Version of the library.
     */
    std::string version;
};

/**
 * @brief Information the About dialog shows.
 *
 * Every member is compiled into the binary: the values are recorded while the
 * application is built (see `cmake/GenerateAboutInfo.cmake`) and are never
 * looked up at run time, so opening the dialog does not call git, read a
 * version file or touch the file system.
 *
 * The structure is free of wxWidgets types on purpose: the information is built
 * by the packer core and can be checked by the in-process unit tests, while the
 * dialog only lays the values out.
 */
struct AboutInfo
{
    /**
     * @brief Version of the CMake project, e.g. `"0.1.0"`.
     */
    std::string version;

    /**
     * @brief Local time of the build, formatted as `YYYY-MM-DD HH:MM:SS`.
     */
    std::string build_date;

    /**
     * @brief Abbreviated revision the binary was built from.
     *
     * `"unknown"` when the binary was built without git.
     */
    std::string git_revision;

    /**
     * @brief Branch the built revision belongs to.
     *
     * `"unknown"` when the binary was built without git.
     */
    std::string git_branch;

    /**
     * @brief Whether the working tree held uncommitted changes while building.
     */
    bool git_dirty = false;

    /**
     * @brief Third-party libraries of the binary, ordered by name.
     */
    std::vector<DependencyInfo> dependencies;
};

/**
 * @brief The one sentence which tells what the application does.
 *
 * The text is the complete functional description of the About dialog: it
 * replaces the multi paragraph explanation the dialog used to show.
 */
inline constexpr const char* kAboutSummary =
    "Package an installed application into a portable, sandboxed archive.";

/**
 * @brief Get the build information recorded when the application was built.
 *
 * The values never change, so the same instance is handed out by every call.
 *
 * @return The build information of this binary.
 */
const AboutInfo& GetAboutInfo();

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ABOUT_INFO_HPP
