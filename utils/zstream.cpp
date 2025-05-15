#include <stdexcept>
#include <zlib.h>
#include "zstream.hpp"

#define ZLIB_CHUNK 16384

struct appbox::ZDeflateStream::Data
{
    HANDLE   hFile;
    z_stream stream;
    uint8_t  cache_in[ZLIB_CHUNK];
    uint8_t  cache_out[ZLIB_CHUNK];
};

appbox::ZDeflateStream::ZDeflateStream(HANDLE hFile, int level)
{
    m_data = new Data;
    m_data->hFile = hFile;

    memset(&m_data->stream, 0, sizeof(m_data->stream));
    if (deflateInit(&m_data->stream, level) != Z_OK)
    {
        throw std::runtime_error("deflateInit failed");
    }
}

appbox::ZDeflateStream::~ZDeflateStream()
{
    m_data->stream.next_in = m_data->cache_in;
    m_data->stream.avail_in = 0;

    do
    {
        m_data->stream.next_out = m_data->cache_out;
        m_data->stream.avail_out = sizeof(m_data->cache_out);
        ::deflate(&m_data->stream, Z_FINISH);

        DWORD    write_sz = 0;
        unsigned have = sizeof(m_data->cache_out) - m_data->stream.avail_out;
        WriteFile(m_data->hFile, &m_data->cache_out, have, &write_sz, NULL);
    } while (m_data->stream.avail_out == 0);

    ::deflateEnd(&m_data->stream);
    delete m_data;
}

size_t appbox::ZDeflateStream::deflate(const void* data, size_t size)
{
    size_t total_write_sz = 0;
    m_data->stream.next_in = (Bytef*)(data);
    m_data->stream.avail_in = (uInt)size;

    do
    {
        m_data->stream.next_out = m_data->cache_out;
        m_data->stream.avail_out = sizeof(m_data->cache_out);
        if (::deflate(&m_data->stream, Z_NO_FLUSH) == Z_STREAM_ERROR)
        {
            throw std::runtime_error("deflate failed");
        }

        DWORD    write_sz = 0;
        unsigned have = sizeof(m_data->cache_out) - m_data->stream.avail_out;
        if (!WriteFile(m_data->hFile, m_data->cache_out, have, &write_sz, nullptr) ||
            write_sz != have)
        {
            throw std::runtime_error("Failed to write deflate");
        }
        total_write_sz += write_sz;
    } while (m_data->stream.avail_out == 0);

    return total_write_sz;
}

size_t appbox::ZDeflateStream::deflate(HANDLE file)
{
    DWORD  read_sz = 0;
    size_t total_write_sz = 0;

    while (ReadFile(file, m_data->cache_out, sizeof(m_data->cache_out), &read_sz, nullptr) ||
           read_sz == 0)
    {
        total_write_sz += deflate(m_data->cache_out, read_sz);
    }

    return total_write_sz;
}

struct appbox::ZInflateStream::Data
{
    z_stream    stream;
    const void* data;
    size_t      size;
};

appbox::ZInflateStream::ZInflateStream(const void* data, size_t size)
{
    m_data = new Data;
    m_data->data = data;
    m_data->size = size;

    memset(&m_data->stream, 0, sizeof(m_data->stream));
    if (::inflateInit(&m_data->stream) != Z_OK)
    {
        throw std::runtime_error("inflateInit failed");
    }
    m_data->stream.next_in = (Bytef*)data;
    m_data->stream.avail_in = (uInt)size;
}

appbox::ZInflateStream::~ZInflateStream()
{
    ::inflateEnd(&m_data->stream);
    delete m_data;
}

size_t appbox::ZInflateStream::inflate(void* buff, size_t size)
{
    m_data->stream.avail_out = (uInt)size;
    m_data->stream.next_out = (Bytef*)buff;
    int ret = ::inflate(&m_data->stream, Z_NO_FLUSH);
    if (ret == Z_STREAM_ERROR)
    {
        throw std::runtime_error("inflateInflate failed");
    }
    return (ret == Z_STREAM_END) ? 0 : (size - m_data->stream.avail_out);
}
