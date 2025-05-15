#ifndef APPBOX_UTILS_ZSTREAM_HPP
#define APPBOX_UTILS_ZSTREAM_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>

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
    struct Data;
    struct Data* m_data;
};

class ZInflateStream
{
public:
    ZInflateStream(const void* data, size_t size);
    ~ZInflateStream();

public:
    size_t inflate(void* buff, size_t size);

private:
    struct Data;
    struct Data* m_data;
};

} // namespace appbox

#endif
