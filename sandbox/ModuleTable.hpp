#ifndef APPBOX_SANDBOX_MODULE_TABLE_HPP
#define APPBOX_SANDBOX_MODULE_TABLE_HPP

#include <cstddef>
#include <windows.h>

namespace appbox
{

/**
 * @brief Initialization entry of a sandbox module.
 * @return A status code, successful when NT_SUCCESS() is true.
 */
typedef NTSTATUS (*ModuleInitFn)();

/**
 * @brief Deinitialization entry of a sandbox module.
 */
typedef void (*ModuleExitFn)();

/**
 * @brief A pair of module initialization and deinitialization entries.
 */
struct ModuleInitializer
{
    ModuleInitFn fn_init; /* Initialization entry. */
    ModuleExitFn fn_exit; /* Deinitialization entry. */
};

/**
 * @brief Initialize every module of the table in order.
 *
 * When the initialization of a module fails, the modules that were initialized
 * successfully before it are deinitialized in reverse order, each exactly once,
 * and the function returns false. The failing module itself is not
 * deinitialized, because its initialization did not complete.
 *
 * The number of deinitialization calls is bounded by the number of successfully
 * initialized modules, so the function always terminates.
 *
 * @param[in] modules Module table.
 * @param[in] count Number of modules in the table.
 * @return true when every module was initialized, otherwise false.
 */
bool InitModuleTable(const ModuleInitializer* modules, size_t count);

/**
 * @brief Deinitialize every module of the table in reverse order.
 *
 * Every module is deinitialized exactly once. Use this function only after
 * InitModuleTable() returned true, otherwise modules that were never
 * initialized are deinitialized as well.
 *
 * @param[in] modules Module table.
 * @param[in] count Number of modules in the table.
 */
void ExitModuleTable(const ModuleInitializer* modules, size_t count);

} // namespace appbox

#endif // APPBOX_SANDBOX_MODULE_TABLE_HPP
