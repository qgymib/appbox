#ifndef APPBOX_PACKER_CORE_ZIP_WRITER_HPP
#define APPBOX_PACKER_CORE_ZIP_WRITER_HPP

#include "CompressionLevel.hpp"
#include <cstddef>
#include <cstdint>
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
 *
 * Every file entry is written with the compression level of the session: a
 * level of `StoreOnly` keeps the content of the entry as it is, a level above
 * it deflates the entry and lets libzip keep the smaller of the deflated and
 * the stored data, so an entry which does not compress never grows. Directory
 * entries carry no content and are written as they are.
 */
class ZipWriter
{
public:
    /**
     * @brief Create (truncate) a zip archive.
     * @param[in] path Archive path.
     * @param[in] level Compression level of the file entries; the default level
     *                  of a session is used when the caller names none.
     * @throw std::runtime_error The archive could not be created.
     */
    explicit ZipWriter(const std::wstring& path, CompressionLevel level = kDefaultCompressionLevel);

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
     * @brief Ask libzip for the compression of one entry of the archive.
     *
     * The entry keeps the content of its source while the level stores it, and
     * is deflated with the level of the session otherwise. The call is made for
     * every file entry right after it was added, because libzip writes the
     * content only while the archive is closed.
     *
     * @param[in] index Index of the entry inside the archive.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetEntryCompression(std::uint64_t index, std::string& error);

    /**
     * @brief Format the pending libzip error.
     * @param[in] context Description of the failed operation.
     * @return The composed error message.
     */
    std::string FormatError(const char* context) const;

    zip*                           archive_ = nullptr;
    CompressionLevel               level_ = kDefaultCompressionLevel;
    std::vector<std::vector<char>> buffers_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ZIP_WRITER_HPP
