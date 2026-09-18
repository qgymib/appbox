#include "ZipWriter.hpp"
#include "WString.hpp"
#include <zip.h>
#include <spdlog/spdlog.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace appbox
{

ZipWriter::ZipWriter(const std::wstring& path)
{
    int error = 0;
    archive_ = zip_open(WideToUTF8(path).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (archive_ == nullptr)
    {
        zip_error_t detail;
        zip_error_init(&detail);
        zip_error_set(&detail, error, errno);
        const std::string message(zip_error_strerror(&detail));
        zip_error_fini(&detail);
        throw std::runtime_error("failed to create the zip archive: " + message);
    }
}

ZipWriter::~ZipWriter()
{
    if (archive_ != nullptr)
    {
        SPDLOG_WARN("discarding an unclosed zip archive");
        zip_discard(archive_);
        archive_ = nullptr;
    }
}

bool ZipWriter::AddDirectory(const std::string& entry, std::string& error)
{
    if (archive_ == nullptr)
    {
        error = "the archive is already closed";
        return false;
    }

    if (zip_dir_add(archive_, entry.c_str(), ZIP_FL_ENC_UTF_8) < 0)
    {
        error = FormatError("failed to add the directory entry");
        return false;
    }
    return true;
}

bool ZipWriter::AddFileBuffer(const std::string& entry, const void* data, std::size_t size, std::string& error)
{
    if (archive_ == nullptr)
    {
        error = "the archive is already closed";
        return false;
    }

    /*
     * zip_source_buffer does not copy the data: it must stay alive until
     * zip_close. Keep an owned copy next to the writer.
     */
    const auto* begin = static_cast<const char*>(data);
    buffers_.emplace_back(begin, begin + size);
    const auto& buffer = buffers_.back();

    zip_source* source = zip_source_buffer(archive_, buffer.data(), buffer.size(), 0);
    if (source == nullptr)
    {
        error = FormatError("failed to create the file source");
        return false;
    }

    if (zip_file_add(archive_, entry.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
    {
        /* The source is not owned by the archive when the add failed. */
        zip_source_free(source);
        error = FormatError("failed to add the file entry");
        return false;
    }
    return true;
}

bool ZipWriter::AddFileDisk(const std::wstring& disk_path, const std::string& entry, std::string& error)
{
    if (archive_ == nullptr)
    {
        error = "the archive is already closed";
        return false;
    }

    /* libzip interprets file names as UTF-8 and converts them to UTF-16. */
    const auto path = WideToUTF8(disk_path);
    zip_source* source = zip_source_file(archive_, path.c_str(), 0, -1);
    if (source == nullptr)
    {
        error = FormatError("failed to create the file source");
        return false;
    }

    if (zip_file_add(archive_, entry.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
    {
        zip_source_free(source);
        error = FormatError("failed to add the file entry");
        return false;
    }
    return true;
}

bool ZipWriter::Close(std::string& error)
{
    if (archive_ == nullptr)
    {
        error = "the archive is already closed";
        return false;
    }

    if (zip_close(archive_) < 0)
    {
        error = FormatError("failed to write the zip archive");
        zip_discard(archive_);
        archive_ = nullptr;
        return false;
    }

    archive_ = nullptr;
    /* The archive consumed the buffers, they can be released now. */
    buffers_.clear();
    return true;
}

std::string ZipWriter::FormatError(const char* context) const
{
    std::string message(context);
    if (archive_ != nullptr)
    {
        const char* detail = zip_strerror(archive_);
        if (detail != nullptr)
        {
            message += ": ";
            message += detail;
        }
    }
    return message;
}

} // namespace appbox
