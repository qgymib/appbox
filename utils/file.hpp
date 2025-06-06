#ifndef APPBOX_UTILS_FILE_HPP
#define APPBOX_UTILS_FILE_HPP

#include "winapi.hpp"
#include <pathcch.h>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include "wstring.hpp"
#include "macros.hpp"

namespace appbox
{

struct FileEntry
{
    std::wstring name;             /* Entry name. */
    std::wstring path;             /* Entry path. */
    DWORD        dwFileAttributes; /* Entry attributes. */
};

struct FileHandle : std::shared_ptr<void>
{
    FileHandle() = default;
    FileHandle(const wchar_t* path, DWORD dwDesiredAccess = GENERIC_READ,
               DWORD                 dwShareMode = FILE_SHARE_READ,
               LPSECURITY_ATTRIBUTES lpSecurityAttributes = nullptr,
               DWORD                 dwCreationDisposition = OPEN_EXISTING,
               DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL, HANDLE hTemplateFile = nullptr)
        : std::shared_ptr<void>(CreateFileW(path, dwDesiredAccess, dwShareMode,
                                            lpSecurityAttributes, dwCreationDisposition,
                                            dwFlagsAndAttributes, hTemplateFile),
                                CloseHandle)
    {
        if (get() == INVALID_HANDLE_VALUE)
        {
            std::string msg = "Failed to open file: " + appbox::wcstombs(path, CP_UTF8);
            throw std::runtime_error(msg);
        }
    }
};

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
 */
inline void ReadFileSized(HANDLE file, void* buff, size_t size)
{
    DWORD except_read_sz = (DWORD)size;
    DWORD read_sz = 0;
    if (!ReadFile(file, buff, except_read_sz, &read_sz, nullptr) || read_sz != except_read_sz)
    {
        throw std::runtime_error("Failed to read file");
    }
}

/**
 * Reads the entire content of a file into a string.
 *
 * This function opens the file specified by the given path, reads its content
 * in chunks, and concatenates it into a single string. The file is expected to
 * be readable and accessible; otherwise, an exception is thrown.
 *
 * @param[in] path The file path as a wide string. It specifies the file to read.
 *                 The path must refer to an existing file with adequate access permissions.
 *
 * @return A string containing the full content of the file. If the file is empty,
 *         an empty string is returned.
 *
 * @throws std::runtime_error If the file cannot be opened or read due to an error.
 */
inline std::string ReadFileAll(const std::wstring& path)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("Failed to open file");
    }

    std::string content;
    char        buff[4096];
    DWORD       read_sz;
    while (::ReadFile(hFile, buff, sizeof(buff), &read_sz, nullptr) && read_sz != 0)
    {
        content.append(buff, read_sz);
    }
    CloseHandle(hFile);

    return content;
}

/**
 * Writes a specified amount of data to a file.
 *
 * This function writes `size` bytes from the memory area pointed to by `buff`
 * into the file represented by the given HANDLE `file`. If the write operation
 * fails or if the number of bytes written differs from the expected `size`,
 * an exception is thrown.
 *
 * @param[in] file A valid Windows file HANDLE to which data will be written.
 * @param[in] buff A pointer to the buffer containing the data to be written.
 *        The buffer must hold at least `size` bytes of valid data.
 * @param[in] size The number of bytes to write to the file.
 *
 * @throw std::runtime_error Thrown if the write operation fails or if the written
 *        byte count does not match the requested size.
 */
inline void WriteFileSized(HANDLE file, const void* buff, size_t size)
{
    DWORD write_sz = 0;
    if (!WriteFile(file, buff, (DWORD)size, &write_sz, nullptr) || write_sz != (DWORD)size)
    {
        throw std::runtime_error("Failed to write file");
    }
}

/**
 * Writes the entire content of the provided buffer to a file.
 *
 * This function continuously writes data from the buffer pointed to by `buff`
 * to the file represented by the given HANDLE `file` until `size` bytes have
 * been written or an error occurs during the operation. It ensures all the data
 * is written unless interrupted by an error.
 *
 * @param[in] path File path.
 * @param[in] data A pointer to the buffer containing the data to write.
 *        The buffer must contain at least `size` bytes of valid data.
 * @param[in] size The total number of bytes to write to the file.
 * @param[in] wdCreationDisposition File creation mode.
 */
inline void WriteFileReplace(const std::wstring& path, const void* data, size_t size,
                             DWORD wdCreationDisposition = CREATE_ALWAYS)
{
    FileHandle hFile(path.c_str(), GENERIC_WRITE, 0, nullptr, wdCreationDisposition);
    WriteFileSized(hFile.get(), data, size);
}

/**
 * Retrieves the current file pointer position of the specified file.
 *
 * This function determines the current position of the file pointer in the file
 * referenced by the given HANDLE `file`. It utilizes the Windows API's
 * `SetFilePointerEx` function with no movement (offset of 0) and retrieves
 * the pointer position without altering it.
 *
 * @param[in] file A valid Windows file HANDLE whose current pointer position
 *            is to be queried.
 *
 * @return The current position of the file pointer in bytes from the beginning
 *         of the file. If the operation fails, an exception is thrown.
 *
 * @exception std::runtime_error Thrown if the call to `SetFilePointerEx` fails.
 */
inline uint64_t GetFilePosition(HANDLE file)
{
    LARGE_INTEGER liDistanceToMove, lpNewFilePointer;
    liDistanceToMove.QuadPart = 0;
    lpNewFilePointer.QuadPart = 0;

    if (!SetFilePointerEx(file, liDistanceToMove, &lpNewFilePointer, FILE_CURRENT))
    {
        throw std::runtime_error("SetFilePointerEx() failed");
    }

    return lpNewFilePointer.QuadPart;
}

/**
 * Sets the file pointer to a specific position within the file.
 *
 * This function moves the file pointer of the provided file handle to the specified
 * position based on the given move method. The position is determined by the `distance`
 * parameter, which represents the offset in bytes, and the `moveMethod` parameter,
 * which defines how the offset is applied relative to the current, start, or end of the file.
 *
 * @param[in] file A valid Windows file HANDLE whose pointer is to be moved.
 * @param[in] position The offset in bytes to move the file pointer. It always seek from the begin
 *   of file.
 */
inline void SetFilePosition(HANDLE file, uint64_t position)
{
    LARGE_INTEGER liDistanceToMove;
    liDistanceToMove.QuadPart = position;

    if (!SetFilePointerEx(file, liDistanceToMove, nullptr, FILE_BEGIN))
    {
        throw std::runtime_error("SetFilePointerEx() failed");
    }
}

/**
 * Writes a specified amount of data to a file at a given position.
 *
 * This function writes `size` bytes of data from the buffer `buff` into the file
 * represented by the given HANDLE `file` at the specified position `position`.
 * The position indicates the offset in the file where the write operation begins.
 *
 * @param[in] file A valid Windows file HANDLE to which data will be written.
 * @param[in] buff A pointer to the buffer containing the data to be written.
 *        The buffer must contain at least `size` bytes of valid data.
 * @param[in] position The position (offset) in the file where the write operation
 *        should begin.
 * @param[in] size The number of bytes to write to the file.
 */
inline void WriteFilePositionSized(HANDLE file, const void* buff, size_t size, uint64_t pos)
{
    uint64_t bak_pos = GetFilePosition(file);
    SetFilePosition(file, pos);
    WriteFileSized(file, buff, size);
    SetFilePosition(file, bak_pos);
}

/**
 * Retrieves the directory name from a given file path.
 *
 * This function extracts and returns the directory portion of a file path
 * provided as input. If the provided path does not include a directory
 * component, the function may return an empty string or a default representation
 * depending on the implementation.
 *
 * @param[in] path A string representing the full path of a file, from which the
 *            directory name should be extracted.
 *
 * @return A string containing the directory name extracted from the provided path.
 *         An empty string may be returned if no directory component is present in
 *         the input path.
 */
inline std::wstring DirName(const std::wstring& path)
{
    size_t path_sz = path.size();
    size_t malloc_sz = path_sz + 1;

    wchar_t* path_buf = new wchar_t[malloc_sz];
    {
        int write_sz = _snwprintf_s(path_buf, malloc_sz, _TRUNCATE, L"%s", path.c_str());
        PathCchRemoveFileSpec(path_buf, MIN(write_sz, (int)path_sz));
    }
    std::wstring result(path_buf);
    delete[] path_buf;

    return result;
}

/**
 * Extracts the base name (file name) from a given file path.
 *
 * This function takes a file path as input and returns the base name,
 * which is the portion of the path following the last path separator ('\').
 * If the input string does not contain a path separator, the original string
 * is returned unchanged.
 *
 * @param[in] path The full path as a wide string.
 * @return The base name extracted from the input path as a wide string.
 */
inline std::wstring BaseName(const std::wstring& path)
{
    std::wstring            result = path;
    std::wstring::size_type pos = result.find_last_of(L'\\');
    if (pos != std::wstring::npos)
    {
        result = result.substr(pos + 1);
    }
    return result;
}

/**
 * Retrieves a list of files and directories from a specified directory path.
 *
 * This function enumerates files and subdirectories in the directory specified
 * by the `path` parameter. If `recursive` is true, it includes files and directories
 * from all subdirectories recursively. Each file and directory is represented as
 * a `FileEntry` object, which contains metadata about the file or directory.
 *
 * @param[in] path The path to the directory to be scanned. Must be a valid Windows path.
 *
 * @return A vector of `FileEntry` objects representing the files and directories
 *         found within the specified path. Throws a runtime exception if the
 *         path is invalid or if any error occurs during file enumeration.
 */
inline std::vector<FileEntry> ListFiles(const std::wstring& path)
{
    std::vector<FileEntry> result;
    DWORD                  attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        throw std::runtime_error("GetFileAttributesW() failed");
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        FileEntry entry;
        entry.name = BaseName(path);
        entry.path = path;
        entry.dwFileAttributes = attributes;
        result.push_back(entry);
        return result;
    }

    /* Ensure the path ends with a backslash and add the wildcard */
    std::wstring formatPath = path;
    if (formatPath.back() != L'\\')
    {
        formatPath += L'\\';
    }
    /* Search for everything in the directory */
    std::wstring searchPath = formatPath + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE           hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("FindFirstFileW() failed");
    }

    do
    {
        std::wstring itemName = ffd.cFileName;
        if (itemName == L"." || itemName == L"..")
        {
            continue;
        }

        FileEntry entry;
        entry.name = itemName;
        entry.path = formatPath + itemName;
        entry.dwFileAttributes = ffd.dwFileAttributes;
        result.push_back(entry);
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);
    return result;
}

/**
 * Retrieves a formatted, localized error message string corresponding to a given error code.
 *
 * This function uses the Windows API to format an error message based
 * on the specified error code and returns the message as a wide string.
 * Memory allocated for the message buffer is freed after use.
 *
 * @param[in] errCode The error code for which the corresponding message is to be retrieved.
 * @return A localized and formatted error message string corresponding to the error code.
 */
inline std::wstring ErrorMessage(DWORD errCode)
{
    LPWSTR       messageBuffer = nullptr;
    size_t       size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, errCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                       (LPWSTR)&messageBuffer, 0, nullptr);
    std::wstring result(messageBuffer, size);
    LocalFree(messageBuffer);
    return result;
}

/**
 * Creates a nested directory structure.
 *
 * This function ensures that the full directory path specified by `path` is created,
 * including any necessary intermediate directories that do not already exist.
 * If the directories are successfully created or already exist, the operation is considered
 * successful.
 *
 * @param[in] path A string specifying the full path of the directory to be created.
 *                 The path may include multiple nested directories.
 */
inline void CreateNestedDirectory(const std::wstring& path)
{
    DWORD        errCode;
    std::wstring bak_path = path.c_str(); // ensure deep copy.
    for (wchar_t* p = &bak_path[0]; *p; ++p)
    {
        if (*p == L'\\' || *p == L'/')
        {
            *p = L'\0';
            CreateDirectoryW(bak_path.c_str(), nullptr);
            *p = L'\\';
        }
    }
    if (!CreateDirectoryW(&bak_path[0], nullptr) &&
        (errCode = GetLastError()) != ERROR_ALREADY_EXISTS)
    {
        std::wstring errmsg = appbox::ErrorMessage(errCode);
        std::wstring msg = std::wstring(L"CreateDirectoryW() `") + path + L"` failed: " + errmsg;
        throw std::runtime_error(appbox::wcstombs(msg.c_str(), CP_UTF8));
    }
}

/**
 * Retrieves the full path of the executable file of the current process.
 *
 * This function uses the Windows API to obtain the absolute path
 * to the executable file of the running application.
 *
 * @return A wide string containing the full path of the executable file.
 */
inline std::wstring GetExePath()
{
    wchar_t path[4096];
    GetModuleFileNameW(nullptr, path, ARRAY_SIZE(path));
    return path;
}

/**
 * Retrieves the size of a specified file in bytes.
 *
 * This function calculates and returns the size of a file
 * located with given file handle.
 *
 * @param[in] hFile The file handle whose size is to be determined.
 * @return The size of the file in bytes.
 */
inline uint64_t GetFileSize(HANDLE hFile)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        throw std::runtime_error("GetFileSizeEx failed");
    }
    return fileSize.QuadPart;
}

/**
 * Concatenates a name to a given file path, ensuring proper path separator.
 *
 * This function appends the given `name` to the `path`. If the `path` does not
 * already end with a backslash ('\'), one is appended before adding the `name`.
 *
 * @param[in,out] path The file path to which the name will be appended. The path
 *                     may be modified to include a backslash if it does not
 *                     already have one at the end.
 * @param[in] name The name to append to the path. This can represent a file
 *                 name or a directory name.
 */
inline void PathConcat(std::wstring& path, const std::wstring& name)
{
    if (path.back() != L'\\')
    {
        path += L'\\';
    }
    path += name;
}

} // namespace appbox

#endif
