#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "utils/winapi.hpp"
#include "utils/inject.hpp"

namespace appbox
{

struct AppBox
{
    AppBox();
    HINSTANCE  hinstDLL; /* A handle to the DLL module. */
    InjectData inject;   /* Inject data. */
};

extern AppBox* G;

} // namespace appbox

#endif
