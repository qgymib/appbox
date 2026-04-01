#ifndef APPBOX_COMMON_INJECT_DATA_HPP
#define APPBOX_COMMON_INJECT_DATA_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

struct InjectData
{
    std::string pipe_path;      /* Named pipe path. */
    std::string sandbox32_path; /* Path to 32-bit sandbox dll path. */
    std::string sandbox64_path; /* Path to 64-bit sandbox dll path. */

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(InjectData, pipe_path, sandbox32_path, sandbox64_path)
};

} // namespace appbox

#endif // APPBOX_COMMON_INJECT_DATA_HPP
