#ifndef APPBOX_CONFIG_HPP
#define APPBOX_CONFIG_HPP

/**
 * @brief Set the default log max lines for AppBox loader.
 */
#ifndef APPBOX_LOADER_DEFAULT_LOG_MAX_LINE_LENGTH
#define APPBOX_LOADER_DEFAULT_LOG_MAX_LINE_LENGTH 8000
#endif

/**
 * @brief GUID for sandbox dll inject.
 */
#ifndef APPBOX_SANDBOX_GUID
#define APPBOX_SANDBOX_GUID { 0x937d703e, 0x3ba4, 0x11f0, { 0x2c, 0xcf, 0x67, 0x72, 0xf1, 0xd5, 0xc7, 0xf5 } }
#endif

#endif
