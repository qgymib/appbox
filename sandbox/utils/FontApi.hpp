#ifndef APPBOX_SANDBOX_UTILS_FONT_API_HPP
#define APPBOX_SANDBOX_UTILS_FONT_API_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox
{
namespace fonts
{

/**
 * @brief Load the modules which carry the font resources of the sandbox.
 *
 * The entry point the hooks intercept lives in `win32u.dll` and the documented
 * entry point the isolation module calls lives in `gdi32.dll`, which brings the
 * other module with it. A process loads both on demand, so the sandbox loads
 * them itself before it resolves the entry points of its hooks: a hook which
 * cannot be resolved is fatal in isolation mode.
 *
 * The call is idempotent, so the isolation module and the hook table can both
 * ask for the modules.
 *
 * @return true when the modules are loaded.
 */
bool LoadFontModules();

/**
 * @brief Resolve a font file path of the view to the file of the layer which holds it.
 *
 * The path is the one a caller hands to the font API of the system, in NT form
 * (`\??\C:\Windows\Fonts\MyFont.ttf`). The function maps it into the view of the
 * sandbox and resolves it there: a path which a sandbox layer holds is answered
 * with the file of that layer, which is the file the kernel has to open, because
 * the font driver reads it without passing the hooks of the sandbox.
 *
 * A path which only the host filesystem holds, a path which the isolation hides
 * and a path which no layer holds answer false: the caller forwards its call
 * unchanged, so the kernel reports what the view reports.
 *
 * @param[in] path Path of a font file in NT form.
 * @param[out] resolved File of the layer which holds the path.
 * @return true when a sandbox layer holds the path.
 */
bool ResolveFontFilePath(const std::wstring& path, std::wstring& resolved);

/**
 * @brief Rewrite the buffer of a font resource call.
 *
 * The buffer of the call carries one path in NT form, and the character count
 * the caller declares includes the terminating NUL of that path. Only the bytes
 * the caller declared are read: a buffer which does not carry a terminator
 * within its length is left alone.
 *
 * The buffer is rewritten only when a sandbox layer holds the file, see
 * ResolveFontFilePath(). The count of the rewritten buffer is the count of the
 * caller plus the difference of the two path lengths, which keeps whatever
 * convention the caller used.
 *
 * @param[in] files Buffer of the call.
 * @param[in] cwc Characters of the buffer, the terminating NUL included.
 * @param[out] rewritten Buffer which names the file of the layer.
 * @param[out] rewritten_cwc Characters of the rewritten buffer.
 * @return true when the call has to be forwarded with the rewritten buffer.
 */
bool RewriteFontFileBuffer(const wchar_t* files, ULONG cwc, std::wstring& rewritten, ULONG& rewritten_cwc);

/**
 * @brief Whether a file name carries an extension GDI loads as a font.
 *
 * @param[in] name File name of a directory entry.
 * @return true when the extension names a font resource.
 */
bool IsFontFileName(const std::wstring& name);

/**
 * @brief Convert a NT path into a DOS path.
 *
 * The font API of the sandbox is called with the path of a file of a layer,
 * which is a NT path (`\??\D:\sandbox\app\filesystem\#Fonts#\MyFont.ttf`), while
 * the documented entry point of the system expects a DOS path
 * (`D:\sandbox\app\filesystem\#Fonts#\MyFont.ttf`).
 *
 * @param[in] nt_path NT path of a file.
 * @param[out] dos_path DOS path of the same file.
 * @return true when the path is a local NT path.
 */
bool DosPathOf(const std::wstring& nt_path, std::wstring& dos_path);

} // namespace fonts
} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_FONT_API_HPP
