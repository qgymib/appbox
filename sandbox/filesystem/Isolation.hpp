#ifndef APPBOX_SANDBOX_FILESYSTEM_ISOLATION_HPP
#define APPBOX_SANDBOX_FILESYSTEM_ISOLATION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "IsolationTable.hpp"

namespace appbox
{
namespace filesystem
{

/**
 * @brief The isolation modes of the virtual filesystem of the sandbox.
 *
 * The module loads the isolation file the packer wrote into the overlay of the
 * archive (see `common/FilesystemIsolation.hpp` for the schema) and fills the
 * table of the sandbox instance with the modes of the workspace. The loader
 * passes the path of the file in the injected configuration.
 *
 * A missing file, a missing configuration or a malformed document is not an
 * error: the sandbox then behaves like one without an isolation file, in which
 * every entry keeps the default mode of its kind and the host filesystem stays
 * visible. Only a failure of the whole initialization is reported, which is
 * what the module table expects.
 */
class Isolation
{
public:
    /**
     * @brief Load the isolation modes of the virtual filesystem.
     *
     * The module has to be initialized before the hooks are attached, so the
     * file is read through the original entry points of the process.
     *
     * @return Status code.
     */
    static NTSTATUS Init();

    /**
     * @brief Drop the isolation modes.
     */
    static void Exit();
};

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_ISOLATION_HPP
