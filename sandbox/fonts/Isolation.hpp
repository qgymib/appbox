#ifndef APPBOX_SANDBOX_FONTS_ISOLATION_HPP
#define APPBOX_SANDBOX_FONTS_ISOLATION_HPP

#include "utils/WinAPI.h"

namespace appbox
{
namespace fonts
{

/**
 * @brief The fonts of the view of the sandbox, loaded into the process.
 *
 * The module loads every font file the view shows in the font directory of the
 * system, which is the folder the `Fonts` preset directory of the packer maps
 * its layer to (see docs/FontsIsolation.md). The files are loaded with
 * `AddFontResourceExW(..., FR_PRIVATE, ...)`, which adds them to the font table
 * of this process only: the packaged application creates and enumerates the
 * fonts as if they were installed, while the font directory, the font table and
 * the registry of the host stay untouched.
 *
 * The module has to be initialized before the hooks are attached, because it
 * reads the layers of the view through the original entry points of the
 * process. The fonts are loaded in the order the layers of the view have, so a
 * font of a patch package overrides the font of the same name of the archive,
 * and a file which the isolation hides is not loaded at all.
 */
class Isolation
{
public:
    /**
     * @brief Load the fonts the view holds into the font table of the process.
     *
     * A view without a font directory, a missing module and a single font which
     * cannot be loaded are not errors: the sandbox then behaves like one
     * without packaged fonts, which is what keeps a broken font from failing
     * the start of the application.
     *
     * @return Status code.
     */
    static NTSTATUS Init();

    /**
     * @brief Drop the paths of the loaded fonts.
     *
     * The fonts themselves stay in the font table of the process: the module is
     * only detached while the process is torn down, and the kernel reclaims the
     * table with the process.
     */
    static void Exit();
};

} // namespace fonts
} // namespace appbox

#endif // APPBOX_SANDBOX_FONTS_ISOLATION_HPP
