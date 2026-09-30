#ifndef APPBOX_LAUNCHER_UTILS_MD5_HPP
#define APPBOX_LAUNCHER_UTILS_MD5_HPP

#include <string>

namespace appbox
{

/**
 * @brief Compute the MD5 digest of a file.
 *
 * The digest is the change detector of the patch cache: it decides whether the
 * cache entry of a patch package still describes the package, so a package
 * which the user replaced is extracted again while an unchanged one is reused.
 * MD5 is not a security boundary here, which is why the fast digest of the CNG
 * API of Windows is enough; it needs no key and no session, unlike the legacy
 * CryptoAPI of `advapi32`.
 *
 * The file is read in blocks, so a package of any size is hashed without being
 * held in memory.
 *
 * @param[in] path Path of the file.
 * @param[out] digest Lower case hexadecimal digest (32 characters) on success,
 *                    cleared when the digest cannot be computed.
 * @param[out] error Error description on failure.
 * @return true when the digest was computed.
 */
bool Md5OfFile(const std::wstring& path, std::string& digest, std::string& error);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_MD5_HPP
