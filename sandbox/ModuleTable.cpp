#include "utils/WinAPI.h" /* Must be first include file */
#include "ModuleTable.hpp"

bool appbox::InitModuleTable(const ModuleInitializer* modules, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (!NT_SUCCESS(modules[i].fn_init()))
        {
            /* Roll back the modules initialized so far, in reverse order. */
            for (size_t j = i; j > 0; --j)
            {
                modules[j - 1].fn_exit();
            }

            return false;
        }
    }

    return true;
}

void appbox::ExitModuleTable(const ModuleInitializer* modules, size_t count)
{
    for (size_t i = count; i > 0; --i)
    {
        modules[i - 1].fn_exit();
    }
}
