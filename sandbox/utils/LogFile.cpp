#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "LogFile.hpp"

std::atomic<HANDLE> appbox::LogFile::s_file{ INVALID_HANDLE_VALUE };
std::wstring        appbox::LogFile::s_path;

namespace
{

/** Extension of a program, which is dropped from the name of the log file. */
constexpr const wchar_t kProgramExtension[] = L".exe";

/** Number of the characters of the extension of a program. */
constexpr std::size_t kProgramExtensionSize = 4;

/**
 * @brief Append a number with leading zeros.
 * @param[in,out] out Text the number is appended to.
 * @param[in] value The number.
 * @param[in] digits Number of the digits to append.
 */
void AppendPaddedNumber(std::wstring& out, unsigned value, unsigned digits)
{
    wchar_t buffer[10];

    for (unsigned index = 0; index < digits; ++index)
    {
        buffer[digits - 1 - index] = static_cast<wchar_t>(L'0' + (value % 10));
        value /= 10;
    }

    out.append(buffer, digits);
}

/**
 * @brief Whether a text ends with the extension of a program.
 *
 * The comparison ignores the case, because a file name on Windows does.
 *
 * @param[in] name Name to test.
 * @return true when the name ends with `.exe`.
 */
bool EndsWithProgramExtension(const std::wstring& name)
{
    const std::size_t size = kProgramExtensionSize;
    if (name.size() <= size)
    {
        return false;
    }

    for (std::size_t index = 0; index < size; ++index)
    {
        wchar_t left = name[name.size() - size + index];
        if (left >= L'A' && left <= L'Z')
        {
            left = static_cast<wchar_t>(left - L'A' + L'a');
        }

        if (left != kProgramExtension[index])
        {
            return false;
        }
    }

    return true;
}

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
    /* The name of the program, without its directory. */
    std::wstring program = image_path;
    const auto   separator = program.find_last_of(L"\\/");
    if (separator != std::wstring::npos)
    {
        program.erase(0, separator + 1);
    }

    if (EndsWithProgramExtension(program))
    {
        program.resize(program.size() - kProgramExtensionSize);
    }

    if (program.empty())
    {
        /* A process whose image path is not readable still gets a log file. */
        program = L"process";
    }

    std::wstring file_name = program;
    file_name.push_back(L'.');
    AppendPaddedNumber(file_name, utc.wYear, 4);
    AppendPaddedNumber(file_name, utc.wMonth, 2);
    AppendPaddedNumber(file_name, utc.wDay, 2);
    file_name.push_back(L'T');
    AppendPaddedNumber(file_name, utc.wHour, 2);
    AppendPaddedNumber(file_name, utc.wMinute, 2);
    AppendPaddedNumber(file_name, utc.wSecond, 2);
    file_name.append(L"Z.");
    file_name.append(std::to_wstring(pid));
    file_name.append(L".log");

    return file_name;
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
