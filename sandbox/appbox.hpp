#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "utils/winapi.hpp"
#include "utils/inject.hpp"
#include "utils/Instance.hpp"
#include "Utils/FolderConv.hpp"

namespace appbox
{

struct AppBox
{
    AppBox(HINSTANCE hinstDLL);
    InjectConfig config; /* Inject configuration. */

    HINSTANCE hinstDLL;    /* A handle to the DLL module. */
    HMODULE   hNtdll;      /* ntdll.dll */
    HMODULE   hKernel32;   /* kernel32.dll */
    HMODULE   hKernelBase; /* KernelBase.dll */

    Instance<FolderConv> folderConv; /* Folder converter. */
};

extern AppBox* G;

} // namespace appbox

#endif
