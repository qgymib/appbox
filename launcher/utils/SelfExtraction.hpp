#ifndef APPBOX_LAUNCHER_UTILS_SELF_EXTRACTION_HPP
#define APPBOX_LAUNCHER_UTILS_SELF_EXTRACTION_HPP

#include "ZipReader.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Extract the payload appended to the launcher program.
 *
 * A program without the trailer of `common/ExecutablePayload.hpp` is an
 * ordinary launcher: the call succeeds and leaves @p extraction_root empty. A
 * program with the trailer is a self-extracting one: the payload is extracted
 * into `<temporary directory>\AppBox-SFX-<program stem>-<pid>`, which is removed
 * by RemoveExtraction() when the run ends. A stale folder of the same name is
 * deleted first.
 *
 * @param[in] program Path of the launcher program.
 * @param[in] progress Progress callback of the extraction, may be empty.
 * @param[out] extraction_root Folder of the extraction, empty when the program
 *             carries no payload. It is set as soon as the folder exists, so a
 *             failed extraction is cleaned up as well.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the run may continue.
 */
bool ExtractProgramPayload(const std::wstring& program, const ExtractProgressCallback& progress,
                           std::wstring& extraction_root, std::string& error);

/**
 * @brief Remove the extraction of a self-extracting program.
 *
 * The removal is retried while a process of the run may still release the last
 * handle of a file; a removal which still fails is logged and left behind.
 *
 * @param[in] extraction_root Folder returned by ExtractProgramPayload().
 */
void RemoveExtraction(const std::wstring& extraction_root);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_SELF_EXTRACTION_HPP
