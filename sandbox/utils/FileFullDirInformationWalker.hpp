#ifndef APPBOX_SANDBOX_UTILS_FILEFULLDIRINFORMATIONWALKER_HPP
#define APPBOX_SANDBOX_UTILS_FILEFULLDIRINFORMATIONWALKER_HPP

#include "utils/WinAPI.h"
#include <functional>

namespace appbox
{

struct FileFullDirInformationWalker
{

    typedef std::function<bool(PFILE_FULL_DIR_INFORMATION)> Callback;

    /**
     * @brief Walk through PFILE_FULL_DIR_INFORMATION buffer and delete nodes.
     * @param[in] buff PFILE_FULL_DIR_INFORMATION buffer.
     * @param[in] size buffer size.
     * @param[in] cb callback function.
     * @return Valid bytes.
     */
    static size_t Walk(void* buff, size_t size, const Callback& cb);
};

} // namespace appbox

#endif
