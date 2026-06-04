#ifndef APPBOX_SANDBOX_UTILS_DEFINES_HPP
#define APPBOX_SANDBOX_UTILS_DEFINES_HPP
/* clang-format off */

/**
 * @brief Suffix of whiteout file.
 */
#define APPBOX_SANDBOX_WHITEOUT_SUFFIX_W    L".$APPBOX_DELETE$"

/**
 * @brief Name of opaque file.
 */
#define APPBOX_SANDBOX_OPAQUE_NAME_W        L".$APPBOX_OPAQUE$"

/**
 * @brief GUID for sandbox dll inject.
 */
#define APPBOX_SANDBOX_GUID                 \
    { 0x937d703e, 0x3ba4, 0x11f0, { 0x2c, 0xcf, 0x67, 0x72, 0xf1, 0xd5, 0xc7, 0xf5 } }

/* clang-format on */
#endif
