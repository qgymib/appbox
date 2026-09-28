#ifndef APPBOX_TEST_UTILS_FS_ISOLATION_BUILDER_HPP
#define APPBOX_TEST_UTILS_FS_ISOLATION_BUILDER_HPP

#include "FilesystemIsolation.hpp"
#include "loader/Config.hpp"
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief One entry of the isolation file of a test sandbox.
 */
struct FsIsolationEntry
{
    /**
     * @brief Path of the entry in the virtual filesystem.
     *
     * The first component is the layer key of the preset directory, for
     * example `#APPDATA#\MyApp`.
     */
    std::wstring path;

    /**
     * @brief Kind of the entry.
     */
    appbox::FilesystemEntryKind kind = appbox::FilesystemEntryKind::Directory;

    /**
     * @brief Isolation mode of the entry.
     */
    appbox::FilesystemIsolation isolation = appbox::FilesystemIsolation::WriteCopy;
};

/**
 * @brief Write the filesystem isolation file of a test sandbox.
 *
 * The file describes the modes of the virtual filesystem and lives in the
 * overlay root of the configuration, which is where the loader looks for it
 * (`<overlay>/filesystem-isolation.json`). The document is built directly
 * instead of through the packer, so a case also pins the schema a hand written
 * file uses.
 *
 * @param[in] config Loader configuration of the case.
 * @param[in] entries Entries to list.
 * @return true on success.
 */
bool WriteFsIsolationFile(const appbox::LoaderConfig& config, const std::vector<FsIsolationEntry>& entries);

/**
 * @brief Write the raw text of the filesystem isolation file of a test sandbox.
 *
 * A case which pins how the sandbox treats a document it cannot use writes the
 * text itself with this helper, for example a document which is not valid JSON
 * or one of another version.
 *
 * @param[in] config Loader configuration of the case.
 * @param[in] text Text to write.
 * @return true on success.
 */
bool WriteRawFsIsolationFile(const appbox::LoaderConfig& config, const std::string& text);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_FS_ISOLATION_BUILDER_HPP
