#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "InjectData.hpp"

namespace appbox
{

struct Sandbox
{
    Sandbox(HINSTANCE hinstDLL);

    HINSTANCE          hinstDLL;
    appbox::InjectData inject_data;
};

/**
 * @brief Global sandbox instance
 */
extern Sandbox* g_sandbox;

} // namespace appbox

#endif
