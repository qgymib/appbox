#ifndef APPBOX_UTILS_ZSTREAM_HPP
#define APPBOX_UTILS_ZSTREAM_HPP

#include "winapi.hpp"
#include <zlib.h>

#define APPBOX_ZSTREAM_CHUNK_SIZE 16384

namespace appbox
{

class ZDeflateStream
{
public:
    ZDeflateStream(HANDLE hFile, int level);
    virtual ~ZDeflateStream();

public:
    size_t deflate(const void* data, size_t size);
    size_t deflate(HANDLE file);

private:
    HANDLE   hFile;
    z_stream stream;
    uint8_t  cache_in[APPBOX_ZSTREAM_CHUNK_SIZE];
    uint8_t  cache_out[APPBOX_ZSTREAM_CHUNK_SIZE];
};

ZDeflateStream::ZDeflateStream(HANDLE hFile, int level)
{
    this->hFile = hFile;
    memset(&this->stream, 0, sizeof(this->stream));
    if (deflateInit(&this->stream, level) != Z_OK)
    {
        throw std::runtime_error("deflateInit failed");
    }
}

ZDeflateStream::~ZDeflateStream()
{
    this->stream.next_in = this->cache_in;
    this->stream.avail_in = 0;

    do
    {
        this->stream.next_out = this->cache_out;
        this->stream.avail_out = sizeof(this->cache_out);
        ::deflate(&this->stream, Z_FINISH);

        DWORD    write_sz = 0;
        unsigned have = sizeof(this->cache_out) - this->stream.avail_out;
        WriteFile(this->hFile, &this->cache_out, have, &write_sz, NULL);
    } while (this->stream.avail_out == 0);

    ::deflateEnd(&this->stream);
}

size_t ZDeflateStream::deflate(const void* data, size_t size)
{
    size_t total_write_sz = 0;
    this->stream.next_in = (Bytef*)(data);
    this->stream.avail_in = (uInt)size;

    do
    {
        this->stream.next_out = this->cache_out;
        this->stream.avail_out = sizeof(this->cache_out);
        if (::deflate(&this->stream, Z_NO_FLUSH) == Z_STREAM_ERROR)
        {
            throw std::runtime_error("deflate failed");
        }

        DWORD    write_sz = 0;
        unsigned have = sizeof(this->cache_out) - this->stream.avail_out;
        if (!WriteFile(this->hFile, this->cache_out, have, &write_sz, nullptr) || write_sz != have)
        {
            throw std::runtime_error("Failed to write deflate");
        }
        total_write_sz += write_sz;
    } while (this->stream.avail_out == 0);

    return total_write_sz;
}

size_t ZDeflateStream::deflate(HANDLE file)
{
    DWORD  read_sz = 0;
    size_t total_write_sz = 0;

    while (ReadFile(file, this->cache_in, sizeof(this->cache_in), &read_sz, nullptr) &&
           read_sz != 0)
    {
        total_write_sz += deflate(this->cache_in, read_sz);
    }

    return total_write_sz;
}

class ZInflateStream
{
public:
    ZInflateStream(const void* data, size_t size);
    ~ZInflateStream();

public:
    size_t inflate(void* buff, size_t size);

private:
    z_stream m_stream;
};

ZInflateStream::ZInflateStream(const void* data, size_t size)
{
    memset(&m_stream, 0, sizeof(m_stream));
    if (::inflateInit(&m_stream) != Z_OK)
    {
        throw std::runtime_error("inflateInit failed");
    }
    m_stream.next_in = (Bytef*)data;
    m_stream.avail_in = (uInt)size;
}

ZInflateStream::~ZInflateStream()
{
    ::inflateEnd(&m_stream);
}

size_t ZInflateStream::inflate(void* buff, size_t size)
{
    m_stream.avail_out = (uInt)size;
    m_stream.next_out = (Bytef*)buff;
    int ret = ::inflate(&m_stream, Z_NO_FLUSH);
    if (ret == Z_STREAM_ERROR)
    {
        throw std::runtime_error("inflateInflate failed");
    }
    return (ret == Z_STREAM_END) ? 0 : (size - m_stream.avail_out);
}

} // namespace appbox

#endif
