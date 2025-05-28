#ifndef APPBOX_UTILS_INJECT_HPP
#define APPBOX_UTILS_INJECT_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

struct InjectData
{
    std::string sandboxPath; /* Path to sandbox directory. */
    std::string dllPath32;   /* Path to 32bit dll. */
    std::string dllPath64;   /* Path to 64bit dll. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InjectData, sandboxPath, dllPath32, dllPath64);
};

} // namespace appbox

#endif
