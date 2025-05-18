#ifndef APPBOX_UTILS_FILE_HPP
#define APPBOX_UTILS_FILE_HPP

#include "winapi.hpp"

namespace appbox
{

/**
 * Reads a specified amount of data from a file into a buffer.
 *
 * This function attempts to read `size` bytes of data from the file represented
 * by the given HANDLE `file` and stores the data into the memory area pointed to
 * by `buff`. The operation may fail if the file does not contain enough data
 * or if a read error occurs.
 *
 * @param[in] file A valid Windows file HANDLE from which data is to be read.
 * @param[out] buff A pointer to the buffer where the read data will be stored.
 *        The buffer must be pre-allocated with a size of at least `size` bytes.
 * @param[in] size The number of bytes to read from the file.
 *
 * @return `true` if the specified amount of data (`size`) was successfully read,
 *         otherwise `false` (e.g., because the file does not contain enough data,
 *         because of an error during the read operation, or invalid inputs).
 */
inline bool ReadFileRequiredSize(HANDLE file, void* buff, size_t size)
{
    DWORD read_sz = 0;
    return ReadFile(file, buff, size, &read_sz, nullptr) && read_sz == (DWORD)size;
}

} // namespace appbox

#endif
