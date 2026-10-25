#ifndef APPBOX_TEST_UTILS_LAUNCHERCONFIGREADER_HPP
#define APPBOX_TEST_UTILS_LAUNCHERCONFIGREADER_HPP

#include "launcher/Config.hpp"
#include <string>

namespace appbox::test
{

/**
 * @brief Read a resource embedded in a launcher image as bytes.
 *
 * The image is opened as a data file, so the program it carries is never
 * executed. The identifiers are the ones of `launcher/EmbeddedResourceIds.h`.
 *
 * @param[in] image_path Host path of the launcher image.
 * @param[in] resource_id Identifier of the resource in that image.
 * @param[out] bytes The payload of the resource, empty on failure.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the resource was read.
 */
bool ReadLauncherResource(const std::wstring& image_path, int resource_id, std::string& bytes, std::string& error);

/**
 * @brief Read the raw JSON document of the configuration embedded in a
 *        launcher image.
 *
 * The image is opened as a data file, so the program it carries is never
 * executed. The document is the one the packer wrote into the resource
 * `IDR_APPBOX_LAUNCHER_CONFIG` of `launcher/EmbeddedResourceIds.h`.
 *
 * @param[in] image_path Host path of the launcher image.
 * @param[out] json The UTF-8 JSON document, empty on failure.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the document was read.
 */
bool ReadLauncherConfigJson(const std::wstring& image_path, std::string& json, std::string& error);

/**
 * @brief Read the configuration embedded in a launcher image.
 *
 * @param[in] image_path Host path of the launcher image.
 * @param[out] config The configuration of the image, unchanged on failure.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the configuration was read and parsed.
 */
bool ReadLauncherConfig(const std::wstring& image_path, appbox::LauncherConfig& config, std::string& error);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_LAUNCHERCONFIGREADER_HPP
