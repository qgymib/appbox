#ifndef APPBOX_COMMON_ZIP_READER_ERROR_HPP
#define APPBOX_COMMON_ZIP_READER_ERROR_HPP

namespace appbox
{

/**
 * @brief Error code of an extraction.
 *
 * The extraction reports an error code instead of a formatted message, so the
 * caller decides how the failure is shown. `ZipReaderErrorMessage` is the
 * fallback text of the code, which a localized caller can replace with its own
 * text.
 */
enum ZipReaderErrno
{
    ZIPREADER_SUCCESS = 0,               ///< No error.
    ZIPREADER_OPEN_ARCHIVE_FAILED,       ///< The archive cannot be opened.
    ZIPREADER_CREATE_DESTINATION_FAILED, ///< The destination folder cannot be created.
    ZIPREADER_OPEN_ENTRY_FAILED,         ///< An entry of the archive cannot be opened.
    ZIPREADER_READ_ENTRY_FAILED,         ///< An entry of the archive cannot be read.
    ZIPREADER_UNREADABLE_ENTRY_NAME,     ///< An entry name cannot be decoded.
    ZIPREADER_UNSAFE_ENTRY_NAME,         ///< An entry name would escape the destination folder.
    ZIPREADER_CREATE_ENTRY_FAILED,       ///< A file or folder of the archive cannot be created.
    ZIPREADER_WRITE_ENTRY_FAILED,        ///< An entry of the archive cannot be written to disk.
    ZIPREADER_CANCELLED,                 ///< The progress callback aborted the extraction.
};

/**
 * @brief Get the fallback text of an extraction error code.
 *
 * The text is the message the extractor reported before it returned codes and
 * is meant for logs and for callers which do not translate their messages. A
 * localized caller maps the code to its own text instead.
 *
 * @param[in] error Error code to describe.
 * @return The fallback text, empty for ZIPREADER_SUCCESS.
 */
inline const char* ZipReaderErrorMessage(ZipReaderErrno error)
{
    switch (error)
    {
    case ZIPREADER_SUCCESS:
        return "";
    case ZIPREADER_OPEN_ARCHIVE_FAILED:
        return "failed to open the archive";
    case ZIPREADER_CREATE_DESTINATION_FAILED:
        return "failed to create the destination folder";
    case ZIPREADER_OPEN_ENTRY_FAILED:
        return "failed to open an entry of the archive";
    case ZIPREADER_READ_ENTRY_FAILED:
        return "failed to read an entry of the archive";
    case ZIPREADER_UNREADABLE_ENTRY_NAME:
        return "the archive contains an unreadable entry name";
    case ZIPREADER_UNSAFE_ENTRY_NAME:
        return "the archive contains an unsafe entry name";
    case ZIPREADER_CREATE_ENTRY_FAILED:
        return "failed to create a file or folder of the archive";
    case ZIPREADER_WRITE_ENTRY_FAILED:
        return "failed to write a file of the archive";
    case ZIPREADER_CANCELLED:
        return "cancelled by the user";
    }

    return "unknown extraction error";
}

} // namespace appbox

#endif // APPBOX_COMMON_ZIP_READER_ERROR_HPP
