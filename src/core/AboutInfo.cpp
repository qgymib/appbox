#include "AboutInfo.hpp"
#include "AboutInfo.generated.hpp"
#include <iterator>

namespace
{

/**
 * @brief Copy the constants of the generated header into the public model.
 *
 * The generated header only holds `inline constexpr` data so that it stays
 * warning free and cheap to include; the model the dialog and the unit tests
 * work with uses owning strings instead.
 *
 * @return The build information of this binary.
 */
appbox::AboutInfo MakeAboutInfo()
{
    appbox::AboutInfo info;
    info.version = appbox::generated::kApplicationVersion;
    info.build_date = appbox::generated::kBuildDate;
    info.git_revision = appbox::generated::kGitRevision;
    info.git_branch = appbox::generated::kGitBranch;
    info.git_dirty = appbox::generated::kGitDirty;

    /* The generated array already follows the order the dialog shows. */
    info.dependencies.reserve(std::size(appbox::generated::kDependencies));
    for (const auto& dependency : appbox::generated::kDependencies)
    {
        info.dependencies.push_back({dependency.name, dependency.version});
    }

    return info;
}

} // namespace

namespace appbox
{

const AboutInfo& GetAboutInfo()
{
    /*
     * The values are constants of the binary, so they are turned into the model
     * once and handed out by reference afterwards.
     */
    static const AboutInfo info = MakeAboutInfo();
    return info;
}

} // namespace appbox
