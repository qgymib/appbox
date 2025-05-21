#ifndef APPBOX_UTILS_ZSTREAM_HPP
#define APPBOX_UTILS_ZSTREAM_HPP

#include "file.hpp"
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
    size_t   deflate(const void* data, size_t size);
    size_t   deflate(HANDLE file);
    uint64_t written() const;
    void     finish();

private:
    HANDLE    hFile;
    z_streamp m_stream;
    uint64_t  totalWriteBytes;
    uint8_t   cache_in[APPBOX_ZSTREAM_CHUNK_SIZE];
    uint8_t   cache_out[APPBOX_ZSTREAM_CHUNK_SIZE];
};

inline ZDeflateStream::ZDeflateStream(HANDLE hFile, int level)
{
    this->hFile = hFile;
    this->totalWriteBytes = 0;
    m_stream = new z_stream;

    memset(m_stream, 0, sizeof(*m_stream));
    if (deflateInit(m_stream, level) != Z_OK)
    {
        throw std::runtime_error("deflateInit failed");
    }
}

inline ZDeflateStream::~ZDeflateStream()
{
    finish();
}

inline size_t ZDeflateStream::deflate(const void* data, size_t size)
{
    size_t total_write_sz = 0;
    m_stream->next_in = (Bytef*)(data);
    m_stream->avail_in = (uInt)size;

    do
    {
        m_stream->next_out = this->cache_out;
        m_stream->avail_out = sizeof(this->cache_out);
        if (::deflate(m_stream, Z_NO_FLUSH) == Z_STREAM_ERROR)
        {
            throw std::runtime_error("deflate failed");
        }

        unsigned have = sizeof(this->cache_out) - m_stream->avail_out;
        appbox::WriteFileSized(this->hFile, &this->cache_out, have);
        total_write_sz += have;
    } while (m_stream->avail_out == 0);

    this->totalWriteBytes += total_write_sz;
    return total_write_sz;
}

inline size_t ZDeflateStream::deflate(HANDLE file)
{
    DWORD  read_sz = 0;
    size_t total_write_sz = 0;

    while (ReadFile(file, this->cache_in, sizeof(this->cache_in), &read_sz, nullptr) &&
           read_sz != 0)
    {
        total_write_sz += deflate(this->cache_in, read_sz);
    }

    this->totalWriteBytes += total_write_sz;
    return total_write_sz;
}

inline void ZDeflateStream::finish()
{
    if (m_stream == nullptr)
    {
        return;
    }

    m_stream->next_in = Z_NULL;
    m_stream->avail_in = 0;

    do
    {
        m_stream->next_out = this->cache_out;
        m_stream->avail_out = sizeof(this->cache_out);
        ::deflate(m_stream, Z_FINISH);

        unsigned have = sizeof(this->cache_out) - m_stream->avail_out;
        appbox::WriteFileSized(this->hFile, &this->cache_out, have);

        this->totalWriteBytes += have;
    } while (m_stream->avail_out == 0);

    ::deflateEnd(m_stream);

    delete m_stream;
    m_stream = nullptr;
}

inline uint64_t ZDeflateStream::written() const
{
    return this->totalWriteBytes;
}

class ZInflateStream
{
public:
    ZInflateStream(HANDLE file, size_t size);
    ~ZInflateStream();

public:
    std::string inflate();

private:
    HANDLE   mFile;
    size_t   mMaxReadSize;
    size_t   mReadSize;
    z_stream mStream;
    uint8_t  mCacheIn[APPBOX_ZSTREAM_CHUNK_SIZE];
    uint8_t  mCacheOut[APPBOX_ZSTREAM_CHUNK_SIZE];
};

inline ZInflateStream::ZInflateStream(HANDLE file, size_t size)
{
    mFile = file;
    mMaxReadSize = size;

    memset(&mStream, 0, sizeof(mStream));
    if (::inflateInit(&mStream) != Z_OK)
    {
        throw std::runtime_error("inflateInit failed");
    }
}

inline ZInflateStream::~ZInflateStream()
{
    ::inflateEnd(&mStream);
}

inline std::string ZInflateStream::inflate()
{
    DWORD read_sz = 0;
    DWORD cacheSize = MIN(sizeof(mCacheIn), mMaxReadSize);

    if (cacheSize == 0)
    {
        return std::string();
    }
    if (!::ReadFile(mFile, mCacheIn, cacheSize, &read_sz, nullptr))
    {
        throw std::runtime_error("ReadFile() failed");
    }
    if (read_sz == 0)
    {
        return std::string();
    }
    mMaxReadSize -= read_sz;

    mStream.next_in = (Bytef*)mCacheIn;
    mStream.avail_in = (uInt)read_sz;

    std::string result;
    do
    {
        mStream.avail_out = sizeof(mCacheOut);
        mStream.next_out = (Bytef*)mCacheOut;
        int ret = ::inflate(&mStream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR)
        {
            throw std::runtime_error("inflateInflate failed");
        }

        unsigned have = sizeof(mCacheOut) - mStream.avail_out;
        result.append((char*)mCacheOut, have);
    } while (mStream.avail_out == 0);

    return result;
}

} // namespace appbox

#endif
