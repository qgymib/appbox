/**
 * # Packer
 *
 * Pack loader and inject dll and program into one file.
 *
 * ## File layout
 *
 * ```
 * =====================
 * | Loader            |
 * |-------------------|
 * | Meta Length       | # 4 bytes, include null byte, Big-Endian.
 * |-------------------|
 * | Meta              |
 * |-------------------|
 * | Filesystem Length | # 8 bytes, Big-Endian.
 * |-------------------|
 * | Filesystem        |
 * |-------------------|
 * | Magic Code        | # 4 bytes.
 * |-------------------|
 * | OFFSET            | # 4 bytes, Big-Endian.
 * |-------------------|
 * | CRC32             | # 4 bytes, CRC32(Magic Code + OFFSET)
 * =====================
 * ```
 *
 * ## Meta
 *
 * The `Meta Length ` located at `OFFSET`, indect the length of `Meta`. The `Meta` is a string json,
 * with null byte appended.
 *
 * ```
 * {
 *     "process": Object,
 *     "sandbox": Object,
 *     "startup": Object,
 * }
 * ```
 *
 * ### process
 *
 * ```
 * {
 *     "cwd": Object,
 *     "spawn_child_within_sandbox": Object,
 *     "shutdown_process_tree_on_root_exit": Boolean
 * }
 * ```
 *
 * #### cwd (NOTSUP)
 *
 * The current working directory.
 *
 * + (number)1: Use startup file directory.
 * + (number)2: Use current directory.
 * + string: Use specified path.
 *
 * #### spawn_child_within_sandbox (NOTSUP)
 *
 * Spawn child processes within virtualized environment.
 *
 * ```
 * {
 *     "mode": Boolean,
 *     "list": []
 * }
 * ```
 *
 * + "mode": `true` to treat as whitelist, or `false` to treate as blacklist.
 * + "list": Executable name list. If whitelist, only executables in the list run outside sandbox.
 * If blacklist, only executables in the list run inside sandbox.
 *
 * #### shutdown_process_tree_on_root_exit (NOTSUP)
 *
 * + True: If root process exit, terminate all child process.
 * + False: Do not terminate child prcess.
 *
 * ### sandbox
 *
 * ```
 * {
 *     "location": String,
 *     "reset": Boolean
 * }
 * ```
 *
 * #### location (NOTSUP)
 *
 * Where the modified file store.
 *
 * #### reset (NOTSUP)
 *
 * Reset sandbox on application shutdown.
 *
 * ### startup
 *
 * ```
 * {
 *     "splash_image": String,
 *     "splash_until": Number,
 * }
 * ```
 *
 * #### splash_image (NOTSUP)
 *
 * The startup image, must in `bmp` format, encoding in base64.
 *
 * #### splash_until (NOTSUP)
 *
 * The number of seconds the splash shown. If set to 0, shown until the fist application window
 * appears.
 *
 * ## Magic Code
 *
 * ```
 * 0x24 0x42 0x4F 0x58 | $ B O X
 * ```
 *
 */

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;
    return 0;
}
