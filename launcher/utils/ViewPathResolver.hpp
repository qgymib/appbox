#ifndef APPBOX_LAUNCHER_UTILS_VIEWPATHRESOLVER_HPP
#define APPBOX_LAUNCHER_UTILS_VIEWPATHRESOLVER_HPP

#include "sandbox/Config.hpp"
#include "sandbox/filesystem/Resolve.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Resolves a path of the sandbox view into the host path which holds it.
 *
 * The configuration of a run names the programs it starts as paths of the view,
 * for example `#ProgramFiles#\MyApp\app.exe`, while the files themselves live in
 * the layers of the archive: the resources below `app\filesystem` and the
 * overlay below `data\filesystem`. The image of a program usually exists in
 * those layers only, so the launcher has to find the layer which holds it before
 * it can create the process.
 *
 * The class resolves a path with the very same rules a sandboxed process sees:
 * the layers are searched in the order the view prefers them, the whiteout and
 * opaque markers of the overlay hide entries, the isolation modes of the view
 * mask the layers they hide and a reparse point is expanded by the view itself.
 * The rules are implemented once and shared with the sandbox, see
 * `sandbox/filesystem/ResolveCore.hpp`.
 *
 * The instance holds the layers and the isolation modes of one run, so it is
 * built once and asked for every program the run starts.
 */
class ViewPathResolver
{
public:
    /**
     * @brief Build the resolver of one run.
     *
     * The layers and the isolation files are the ones the injected
     * configuration names, so the resolver describes the very view the sandbox
     * of the run mounts. A file which cannot be read is reported and skipped,
     * like the sandbox skips it: a broken patch package must not fail the run.
     *
     * @param[in] config Injected configuration of the run.
     */
    explicit ViewPathResolver(const appbox::SandboxConfig& config);

    /**
     * @brief Resolve the image of one program into the host path which holds it.
     *
     * The path may carry a layer key of the packer (`#ProgramFiles#`), which is
     * expanded to the known folder of this machine first, exactly like the
     * sandbox expands it when the application opens a path of the view.
     *
     * @param[in] view_path Path of the view, in UTF-16.
     * @param[out] host_path Path of the image inside its layer, as a DOS path,
     *                       only set when the call succeeds.
     * @param[out] error Reason the path cannot be resolved, empty on success.
     * @return true when a layer or the host filesystem holds the image.
     */
    bool Resolve(const std::wstring& view_path, std::wstring& host_path, std::string& error) const;

private:
    appbox::filesystem::ResolveFs      fs_;        /* Layers of the view. */
    appbox::filesystem::IsolationTable isolation_; /* Isolation modes of the view. */
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_VIEWPATHRESOLVER_HPP
