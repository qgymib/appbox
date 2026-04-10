#ifndef APPBOX_COMMON_SET_LOG_LEVEL_HPP
#define APPBOX_COMMON_SET_LOG_LEVEL_HPP

#include <string>

namespace appbox
{

/**
 * @brief Set the log level for the application
 * @param[in] level Level string.
 */
void SetLogLevel(const std::wstring& level);

}

#endif //APPBOX_COMMON_SET_LOG_LEVEL_HPP
