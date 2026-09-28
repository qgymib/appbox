#ifndef APPBOX_TRACER_TRACEDMODULES_HPP
#define APPBOX_TRACER_TRACEDMODULES_HPP

#include "tracer/ArmPlan.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace appbox::tracer
{

/**
 * @brief Names of the modules whose functions are traced.
 *
 * The default scope only uses a part of them: the NT entry points live in
 * ntdll, and the name resolution of the network domain has no NT landing point
 * at all, so it lives in ws2_32 and dnsapi. The Win32 API (kernel32) and its
 * implementation (kernelbase) are read as well, because the exhaustive scope
 * (`--all-exports`) covers them.
 *
 * ws2_32 and dnsapi are loaded on demand, so their breakpoints can only be
 * armed once the loader mapped them.
 *
 * @return The lowercase base names without extension.
 */
std::vector<std::wstring> TracedModuleNames();

/**
 * @brief Derive the module name from an image path.
 *
 * The debugger reports image paths in its module load lines; the breakpoint
 * addresses are keyed by the lowercased file name without its extension
 * (`C:\Windows\System32\KERNEL32.DLL` becomes `kernel32`).
 *
 * @param[in] image_path Path as the debugger reported it.
 * @return The module name, or an empty string when the path has no file name.
 */
std::wstring ModuleNameFromImagePath(const std::wstring& image_path);

/**
 * @brief Directory which holds the system DLLs of an image.
 *
 * A 32 bit program runs against the WOW64 copies of the system DLLs, which have
 * their own export tables; the machine type of the target therefore decides
 * which directory is read.
 *
 * @param[in] machine Machine type of the target (IMAGE_FILE_MACHINE_*).
 * @return The directory; the system directory when the WOW64 directory is not
 *         available.
 */
std::filesystem::path SystemDirectoryForMachine(std::uint16_t machine);

/**
 * @brief Parse the traced modules from a directory.
 *
 * @param[in] directory Directory which holds the modules.
 * @return The parsed modules, keyed by their lowercase base name; a module
 *         which can not be read is missing from the result.
 */
ModuleImages LoadTracedModules(const std::filesystem::path& directory);

/**
 * @brief Parse the traced modules from explicit files.
 *
 * This is the path used while a session runs: the debugger reports where each
 * module was loaded from, which is the authoritative location.
 *
 * @param[in] paths Module name (lowercase, without extension) to image file.
 * @return The parsed modules; a module which can not be read is missing.
 */
ModuleImages LoadTracedModules(const std::map<std::wstring, std::filesystem::path>& paths);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_TRACEDMODULES_HPP
