#ifndef APPBOX_COMMON_INJECT_DATA_HPP
#define APPBOX_COMMON_INJECT_DATA_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

struct InjectData
{
    std::string pipe_path;          /* Named pipe path. Encoding in UTF-8. */
    std::string sandbox_path;       /* Path to sandbox root. Encoding in UTF-8. */
    std::string sandbox32_dll_path; /* Path to 32-bit sandbox dll path. Encoding in UTF-8. */
    std::string sandbox64_dll_path; /* Path to 64-bit sandbox dll path. Encoding in UTF-8. */

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(InjectData, pipe_path, sandbox_path, sandbox32_dll_path,
                                   sandbox64_dll_path)
};

} // namespace appbox

#endif // APPBOX_COMMON_INJECT_DATA_HPP
