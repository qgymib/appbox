#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
#include <spdlog/spdlog.h>
#include "BuildCommandLine.hpp"
#include "WString.hpp"
#include "CommandLine.hpp"
#include "Coredump.hpp"

namespace
{

/** Extension of a dump file. */
constexpr const wchar_t* kDumpFileExtension = L".dmp";

/** Upper bound of the walk of the process tree, which guards against a cycle
 *  in the parent links of a snapshot. */
constexpr size_t kMaxProcessTreeSize = 64;

/** Attempts of the dump of one process. */
constexpr int kDumpAttempts = 3;

/** Milliseconds between two attempts of a dump. */
constexpr DWORD kDumpRetryDelayMs = 250;

/**
 * @brief Write a line to the standard error stream.
 *
 * The coredump writer reports the dumps of a timed out test, which must reach
 * the console of the test run whatever the log level of the test is, so the
 * message is written directly and flushed.
 *
 * @param[in] text The line to write, in UTF-8.
 */
void ReportToStderr(const std::string& text)
{
    std::fputs(text.c_str(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

/**
 * @brief Get the name of the image of a process, without the extension.
 * @param[in] pid Process to look at.
 * @return The name of the image, or `process` when it cannot be read.
 */
std::wstring GetProcessImageStem(unsigned long pid)
{
    const std::wstring fallback = L"process";

    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
    {
        return fallback;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    DWORD        size = static_cast<DWORD>(buffer.size());

    std::wstring stem = fallback;
    if (::QueryFullProcessImageNameW(process, 0, buffer.data(), &size))
    {
        buffer.resize(size);
        stem = std::filesystem::path(buffer).stem().wstring();
    }

    ::CloseHandle(process);
    return stem;
}

/**
 * @brief Get the current local time as a file name friendly text.
 * @return The time as `YYYYMMDD-HHMMSS`.
 */
std::wstring GetTimestampText()
{
    SYSTEMTIME time;
    ::GetLocalTime(&time);

    const auto two = [](WORD value) { return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value); };

    std::wstring text;
    text += std::to_wstring(time.wYear);
    text += two(time.wMonth);
    text += two(time.wDay);
    text += L"-";
    text += two(time.wHour);
    text += two(time.wMinute);
    text += two(time.wSecond);
    return text;
}

/**
 * @brief Open a process to dump.
 * @param[in] pid Process to open.
 * @return The handle, or `nullptr` when the process cannot be opened.
 */
HANDLE OpenProcessToDump(unsigned long pid)
{
    /* The handle data of a dump needs the duplication right, which a process
     * with a protected handle table may refuse; the rest of the dump does not
     * need it. */
    HANDLE process =
        ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, pid);
    if (process == nullptr)
    {
        process = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }

    return process;
}

/**
 * @brief Build the path of the dump of one process.
 * @param[in] request The request which names the directory and the tag.
 * @param[in] pid Process the dump belongs to.
 * @return The full path of the dump file.
 */
std::filesystem::path MakeDumpPath(const appbox::test::CoredumpRequest& request, unsigned long pid)
{
    std::wstring name = appbox::test::SanitizeCoredumpTag(request.tag);
    name += L"-";
    name += std::to_wstring(pid);
    name += L"-";
    name += GetProcessImageStem(pid);
    name += L"-";
    name += GetTimestampText();
    name += kDumpFileExtension;

    return request.dump_dir / name;
}

/**
 * @brief Write one dump of an opened process.
 * @param[in] process Handle of the process to dump.
 * @param[in] pid Process to dump.
 * @param[in] dump_path Path of the dump file.
 * @param[out] error Description of the failure, in UTF-8.
 * @return true on success.
 */
bool WriteDumpOnce(HANDLE process, unsigned long pid, const std::filesystem::path& dump_path, std::string& error)
{
    error.clear();

    HANDLE file =
        ::CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = fmt::format("CreateFileW({}) failed: {}", appbox::WideToUTF8(dump_path.wstring()), ::GetLastError());
        return false;
    }

    const MINIDUMP_TYPE dump_type =
        static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo |
                                   MiniDumpWithHandleData | MiniDumpWithUnloadedModules);

    const BOOL written = ::MiniDumpWriteDump(process, static_cast<DWORD>(pid), file, dump_type, nullptr, nullptr,
                                             nullptr);
    if (!written)
    {
        error = fmt::format("MiniDumpWriteDump({}) failed: {}", pid, ::GetLastError());
    }

    ::CloseHandle(file);

    if (!written)
    {
        /* A dump file which exists has to be a usable dump. */
        ::DeleteFileW(dump_path.c_str());
    }

    return written != FALSE;
}

} // namespace

std::wstring appbox::test::GetOwnExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::wstring();
        }

        if (length < buffer.size())
        {
            buffer.resize(length);
            return buffer;
        }

        /* The buffer was too small: the name was truncated. */
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring appbox::test::SanitizeCoredumpTag(const std::wstring& tag)
{
    std::wstring result;
    result.reserve(tag.size());

    for (const wchar_t c : tag)
    {
        const bool is_letter = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
        const bool is_digit = c >= L'0' && c <= L'9';
        const bool is_kept = is_letter || is_digit || c == L'.' || c == L'_' || c == L'-';

        result.push_back(is_kept ? c : L'_');
    }

    if (result.empty())
    {
        result = L"test";
    }

    return result;
}

std::wstring appbox::test::BuildCoredumpWriterCommandLine(const CoredumpRequest& request)
{
    std::vector<std::wstring> arguments;
    arguments.push_back(std::wstring(L"--") + kCoredumpWriterOption + L"=1");
    arguments.push_back(std::wstring(L"--") + kCoredumpPidOption + L"=" + std::to_wstring(request.pid));
    arguments.push_back(std::wstring(L"--") + kCoredumpDirOption + L"=" + request.dump_dir.wstring());
    arguments.push_back(std::wstring(L"--") + kCoredumpTagOption + L"=" + request.tag);

    return appbox::BuildCommandLine(GetOwnExecutablePath(), arguments);
}

bool appbox::test::ParseCoredumpRequest(const std::wstring& command_line, CoredumpRequest& request)
{
    std::wstring value;
    if (!FindCommandLineOption(command_line, kCoredumpWriterOption, value))
    {
        return false;
    }

    CoredumpRequest parsed;

    if (!FindCommandLineOption(command_line, kCoredumpPidOption, value))
    {
        return false;
    }

    wchar_t*           end = nullptr;
    const unsigned long pid = std::wcstoul(value.c_str(), &end, 10);
    if (pid == 0 || end == nullptr || *end != L'\0')
    {
        return false;
    }
    parsed.pid = pid;

    if (!FindCommandLineOption(command_line, kCoredumpDirOption, value) || value.empty())
    {
        return false;
    }
    parsed.dump_dir = value;

    if (FindCommandLineOption(command_line, kCoredumpTagOption, value) && !value.empty())
    {
        parsed.tag = value;
    }
    else
    {
        parsed.tag = L"test";
    }

    request = parsed;
    return true;
}

std::vector<unsigned long> appbox::test::CollectProcessTree(unsigned long root_pid)
{
    std::vector<unsigned long> tree;
    if (root_pid == 0)
    {
        return tree;
    }

    struct ProcessEntry
    {
        unsigned long pid;
        unsigned long parent;
    };

    std::vector<ProcessEntry> processes;
    HANDLE                    snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry;
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);

        if (::Process32FirstW(snapshot, &entry))
        {
            do
            {
                processes.push_back(ProcessEntry{ entry.th32ProcessID, entry.th32ParentProcessID });
            } while (::Process32NextW(snapshot, &entry));
        }

        ::CloseHandle(snapshot);
    }

    const unsigned long self = ::GetCurrentProcessId();

    /* The snapshot is taken before anything is dumped, so the processes which
     * are started afterwards are not part of the tree. */
    std::set<unsigned long>    visited;
    std::vector<unsigned long> frontier;
    frontier.push_back(root_pid);
    visited.insert(root_pid);

    if (root_pid != self)
    {
        tree.push_back(root_pid);
    }

    /* The walk starts at the root whether the root itself is part of the
     * result or not. */
    for (size_t index = 0; index < frontier.size() && frontier.size() < kMaxProcessTreeSize; ++index)
    {
        const unsigned long parent = frontier[index];
        for (const auto& process : processes)
        {
            if (process.parent != parent || process.pid == 0 || process.pid == self)
            {
                continue;
            }

            if (!visited.insert(process.pid).second)
            {
                continue;
            }

            tree.push_back(process.pid);
            frontier.push_back(process.pid);
        }
    }

    return tree;
}

bool appbox::test::WriteCoredump(unsigned long pid, const std::filesystem::path& dump_path, std::string& error)
{
    error.clear();

    HANDLE process = OpenProcessToDump(pid);
    if (process == nullptr)
    {
        error = fmt::format("OpenProcess({}) failed: {}", pid, ::GetLastError());
        return false;
    }

    /*
     * A process which is still starting up cannot be dumped; the dump of the
     * next attempt, a moment later, works. A process which was stopped between
     * two attempts fails every attempt and is reported.
     */
    bool written = false;
    for (int attempt = 0; attempt < kDumpAttempts && !written; ++attempt)
    {
        if (attempt > 0)
        {
            ::Sleep(kDumpRetryDelayMs);
        }

        written = WriteDumpOnce(process, pid, dump_path, error);
    }

    ::CloseHandle(process);
    return written;
}

std::vector<std::filesystem::path> appbox::test::DumpProcessTree(const CoredumpRequest& request,
                                                                 std::vector<std::string>& errors)
{
    std::vector<std::filesystem::path> dumps;

    std::error_code ec;
    std::filesystem::create_directories(request.dump_dir, ec);
    if (ec)
    {
        errors.push_back(fmt::format("create_directories({}) failed: {}", appbox::WideToUTF8(request.dump_dir.wstring()),
                                     ec.message()));
        return dumps;
    }

    for (const unsigned long pid : CollectProcessTree(request.pid))
    {
        const std::filesystem::path dump_path = MakeDumpPath(request, pid);

        std::string error;
        if (WriteCoredump(pid, dump_path, error))
        {
            dumps.push_back(dump_path);
        }
        else
        {
            errors.push_back(fmt::format("process {} ({}): {}", pid, appbox::WideToUTF8(GetProcessImageStem(pid)),
                                         error));
        }
    }

    return dumps;
}

void appbox::test::TerminateProcessTree(unsigned long root_pid, unsigned long excluded_pid)
{
    const std::vector<unsigned long> tree = CollectProcessTree(root_pid);

    /* The deepest process first, so a parent cannot start its child again
     * while the child is stopped. */
    for (auto it = tree.rbegin(); it != tree.rend(); ++it)
    {
        if (*it == excluded_pid || *it == ::GetCurrentProcessId())
        {
            continue;
        }

        HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, *it);
        if (process == nullptr)
        {
            continue;
        }

        ::TerminateProcess(process, 1);
        ::CloseHandle(process);
    }
}

bool appbox::test::RunCoredumpWriterIfRequested()
{
    CoredumpRequest request;
    if (!ParseCoredumpRequest(GetOwnCommandLine(), request))
    {
        return false;
    }

    std::vector<std::string>           errors;
    const std::vector<std::filesystem::path> dumps = DumpProcessTree(request, errors);

    for (const auto& dump : dumps)
    {
        ReportToStderr(fmt::format("coredump: {}", appbox::WideToUTF8(dump.wstring())));
    }

    for (const auto& error : errors)
    {
        ReportToStderr(fmt::format("coredump failed: {}", error));
    }

    return true;
}
