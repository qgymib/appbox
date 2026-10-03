#ifndef APPBOX_TRACER_ARMPLAN_HPP
#define APPBOX_TRACER_ARMPLAN_HPP

#include "tracer/Category.hpp"
#include "tracer/PeImage.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace appbox::tracer
{

/**
 * @brief Marker which a breakpoint prints when it is hit.
 *
 * The marker is part of the contract between the generated debugger script and
 * the output parser: a breakpoint command is `.echo APPBOXHIT <names>; g`, so a
 * hit prints one line and the program continues.
 */
inline constexpr const char* kHitMarker = "APPBOXHIT";

/** One traced module: the image file and its parsed content. */
struct TracedModule
{
    std::filesystem::path path;  ///< Image file of the module.
    PeImage               image; ///< Parsed image.
};

/** Traced modules, keyed by the lowercase base name without extension. */
using ModuleImages = std::map<std::wstring, TracedModule>;

/** Module base addresses of one debugger session, same key as ModuleImages. */
using ModuleBases = std::map<std::wstring, std::uint64_t>;

/** One breakpoint address together with every name which resolves to it. */
struct ArmGroup
{
    std::wstring              module;  ///< Implementation module (lowercase base name).
    std::uint32_t             rva = 0; ///< Offset of the implementation inside the module.
    std::vector<std::wstring> names;   ///< Sorted `module!function` names sharing the address.
};

/**
 * @brief Build the breakpoint plan of the traced modules.
 *
 * A breakpoint can only be placed on an address, while one address can carry
 * several exported names: `ntdll!NtClose` and `ntdll!ZwClose` are two names of
 * one function, and a forwarded export has no code of its own at all
 * (`kernel32!GetCommandLineW` is resolved to `kernelbase!GetCommandLineW` by the
 * loader). The plan therefore groups every name by the address of the
 * implementation it ends up in, so a hit reports all names the call could have
 * used.
 *
 * Only addresses inside an executable section are planned: writing the trap
 * byte of a breakpoint into anything else would corrupt data of the traced
 * process.
 *
 * @param[in] modules Parsed images of the traced modules.
 * @param[in] categories Categories to include; ignored when all_exports is set.
 * @param[in] all_exports Include every executable export instead of the categories.
 * @param[in] module_directory Directory of the traced modules, used to parse a
 *                             module of a forwarder chain which is not part of
 *                             the traced set (an API set DLL).
 * @return The plan, sorted by implementation module and RVA.
 */
std::vector<ArmGroup> BuildArmPlan(const ModuleImages& modules, const std::vector<Category>& categories,
                                   bool all_exports, const std::filesystem::path& module_directory);

/**
 * @brief Build the debugger command lines which arm a plan for one session.
 *
 * @param[in] plan Breakpoint plan.
 * @param[in] bases Module base addresses of the session.
 * @return One one-shot breakpoint command per planned address, in the order of
 *         the plan; groups whose module has no known base are skipped.
 */
std::vector<std::string> BuildArmLines(const std::vector<ArmGroup>& plan, const ModuleBases& bases);

/**
 * @brief Collect the implementation modules of a plan which are not loaded yet.
 *
 * A module the debugger has not mapped can not carry a breakpoint, so a session
 * has to be woken up when it appears. `ws2_32` and `dnsapi` are loaded on
 * demand, which means they are pending at the first prompt of a session while
 * `ntdll`, `kernel32` and `kernelbase` are always mapped before the initial
 * break of the process.
 *
 * @param[in] plan Breakpoint plan.
 * @param[in] bases Module base addresses which are known in the session.
 * @return The module names, sorted and without duplicates.
 */
std::vector<std::wstring> SelectPendingModules(const std::vector<ArmGroup>& plan, const ModuleBases& bases);

/**
 * @brief Collect the groups of a plan which can be armed in a session.
 *
 * A group is armable when the module which holds its implementation has a known
 * base address and its module does not carry the breakpoints of the plan yet.
 * The result is the part of the plan a session still needs, which is the whole
 * plan for a session which was not armed at all and the groups of a module
 * which was loaded later for one which was.
 *
 * @param[in] plan Breakpoint plan.
 * @param[in] bases Module base addresses of the session.
 * @param[in] armed_modules Modules which already carry the breakpoints of the plan.
 * @return The groups to arm, in the order of the plan.
 */
std::vector<ArmGroup> SelectArmable(const std::vector<ArmGroup>& plan, const ModuleBases& bases,
                                    const std::set<std::wstring>& armed_modules);

/**
 * @brief Build the debugger commands which stop a session when a module is loaded.
 *
 * The filter is the mechanism which makes a module that is loaded on demand
 * observable at all: the debugger reports the load, prints its `ModLoad:` line
 * and waits for input, which is the moment the breakpoints of that module can be
 * armed. The filter is a property of the debugger, not of one process, so it
 * stays in effect for the child processes as well.
 *
 * @param[in] modules Modules to watch.
 * @return One `sxe ld:<module>` line per module, in the order of the input.
 */
std::vector<std::string> BuildLoadFilterLines(const std::vector<std::wstring>& modules);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_ARMPLAN_HPP
