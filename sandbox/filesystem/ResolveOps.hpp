#ifndef APPBOX_SANDBOX_FILESYSTEM_RESOLVEOPS_HPP
#define APPBOX_SANDBOX_FILESYSTEM_RESOLVEOPS_HPP

#include "ResolveCore.hpp"

namespace appbox::filesystem
{

/**
 * @brief The operations the sandbox hands to the shared resolver.
 *
 * The sandbox looks a path up with the entry points it saved before it attached
 * its hooks, so a lookup of the resolver never runs through the hooks again and
 * a path of a layer is answered by the file system itself. The diagnostics of
 * the resolution are written into the log file of the process.
 *
 * The operations are the ones of this process and never change, so the call
 * returns a reference to a static structure.
 *
 * @return The operations of the sandbox.
 */
const ResolveOps& SandboxResolveOps();

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_RESOLVEOPS_HPP
