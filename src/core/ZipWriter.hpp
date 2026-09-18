#ifndef APPBOX_PACKER_CORE_ZIP_WRITER_HPP
#define APPBOX_PACKER_CORE_ZIP_WRITER_HPP

#include <cstddef>
#include <string>
#include <vector>

struct zip;

namespace appbox
{

/**
 * @brief Thin RAII wrapper over libzip for creating zip archives.
 *
 * Entry names are UTF-8 strings with forward slash separators, which is the
 * portable zip convention; host paths are wide strings converted internally.
 * Buffered payloads are copied and kept alive by the writer until the
 * archive is closed, as required by zip_source_buffer.
 */
class ZipWriter
{
public:
    /**
     * @brief Create (truncate) a zip archive.
     * @param[in] path Archive path.
     * @throw std::runtime_error The archive could not be created.
     */
    explicit ZipWriter(const std::wstring& path);

    /**
     * @brief Close or discard the archive.
     *
     * Pending errors are logged but not reported; call Close() when the
     * error information matters.
     */
    ~ZipWriter();

    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    /**
     * @brief Add a directory entry.
     *
     * Directory entries keep empty folders and the tree structure of the
     * archive visible to extraction tools.
     *
     * @param[in] entry Entry name, e.g. `"filesystem/#ProgramFiles#/MyApp"`.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddDirectory(const std::string& entry, std::string& error);

    /**
     * @brief Add a file entry from a memory buffer.
     *
     * The buffer content is copied, so the caller may release its data once
     * the call returns.
     *
     * @param[in] entry Entry name.
     * @param[in] data File content.
     * @param[in] size Content size in bytes.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddFileBuffer(const std::string& entry, const void* data, std::size_t size, std::string& error);

    /**
     * @brief Add a file entry from a host file.
     *
     * The file is streamed by libzip, so arbitrarily large files do not
     * occupy archive sized memory.
     *
     * @param[in] disk_path Host file path.
     * @param[in] entry Entry name.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddFileDisk(const std::wstring& disk_path, const std::string& entry, std::string& error);

    /**
     * @brief Flush and close the archive.
     *
     * The archive is discarded on failure, leaving no partial output behind.
     *
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool Close(std::string& error);

private:
    /**
     * @brief Format the pending libzip error.
     * @param[in] context Description of the failed operation.
     * @return The composed error message.
     */
    std::string FormatError(const char* context) const;

    zip*                             archive_ = nullptr;
    std::vector<std::vector<char>>  buffers_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ZIP_WRITER_HPP
