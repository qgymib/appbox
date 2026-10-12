#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "LogFileName.hpp"
#include "LogFile.hpp"

std::atomic<HANDLE> appbox::LogFile::s_file{ INVALID_HANDLE_VALUE };
std::wstring        appbox::LogFile::s_path;

namespace
{

/**
 * @brief Create every directory of a path which does not exist yet.
 *
 * A failure is ignored: the write of the log file reports it either way.
 *
 * @param[in] path Path whose directories are created.
 */
void CreateDirectories(const std::wstring& path)
{
    for (std::size_t index = 1; index <= path.size(); ++index)
    {
        if (index != path.size() && path[index] != L'\\' && path[index] != L'/')
        {
            continue;
        }

        const std::wstring prefix = path.substr(0, index);
        if (prefix.empty() || prefix.back() == L':')
        {
            /* A drive root exists by definition. */
            continue;
        }

        ::CreateDirectoryW(prefix.c_str(), nullptr);
    }
}

} // namespace

std::wstring appbox::LogFile::FileNameOf(const std::wstring& image_path, const SYSTEMTIME& utc, unsigned long pid)
{
    /*
     * The rule is shared with the launcher, which names the log file of its own
     * run with it (see `common/LogFileName.hpp`), so the log files of a run are
     * recognized by one name pattern.
     */
    const appbox::LogFileTimestamp timestamp{ utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond };
    return appbox::LogFileNameOf(image_path, timestamp, pid);
}

bool appbox::LogFile::Open(const std::wstring& dir, const std::wstring& image_path)
{
    if (s_file.load(std::memory_order_relaxed) != INVALID_HANDLE_VALUE)
    {
        /* The file of this process is open already. */
        return true;
    }

    if (dir.empty())
    {
        return false;
    }

    CreateDirectories(dir);

    SYSTEMTIME utc;
    ZeroMemory(&utc, sizeof(utc));
    ::GetSystemTime(&utc);

    std::wstring path = dir;
    if (path.back() != L'\\' && path.back() != L'/')
    {
        path.push_back(L'\\');
    }
    path.append(FileNameOf(image_path, utc, ::GetCurrentProcessId()));

    /*
     * The file is shared for reading and for writing, so the log of a running
     * process can be inspected and a second run of the same process id cannot
     * be blocked by a handle which an earlier run leaked.
     */
    const HANDLE file =
        ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    s_path = path;
    s_file.store(file, std::memory_order_relaxed);
    return true;
}

void appbox::LogFile::Close()
{
    const HANDLE file = s_file.exchange(INVALID_HANDLE_VALUE, std::memory_order_relaxed);
    if (file != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(file);
    }
}

bool appbox::LogFile::IsOpen()
{
    return s_file.load(std::memory_order_relaxed) != INVALID_HANDLE_VALUE;
}

void appbox::LogFile::WriteLine(const std::string& text)
{
    std::string line;
    line.reserve(text.size() + 2);
    line.append(text);
    line.append("\r\n");

    Write(line.data(), line.size());
}

void appbox::LogFile::Write(const char* data, std::size_t size)
{
    const HANDLE file = s_file.load(std::memory_order_relaxed);
    if (file == INVALID_HANDLE_VALUE || data == nullptr || size == 0)
    {
        return;
    }

    std::size_t written_total = 0;
    while (written_total < size)
    {
        DWORD       written = 0;
        const DWORD chunk = static_cast<DWORD>(size - written_total);
        if (!::WriteFile(file, data + written_total, chunk, &written, nullptr) || written == 0)
        {
            /* A log must never fail the caller, so a failed write is dropped. */
            return;
        }

        written_total += written;
    }
}

const std::wstring& appbox::LogFile::Path()
{
    return s_path;
}
