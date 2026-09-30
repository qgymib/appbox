#ifndef APPBOX_SANDBOX_UTILS_LOG_FILE_HPP
#define APPBOX_SANDBOX_UTILS_LOG_FILE_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include <atomic>
#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Log file of one sandboxed process.
 *
 * Every process the sandbox is injected into writes a log file of its own,
 * named after the program, the UTC time it started at and its process id, and
 * placed in the log directory of the run. The loader never carries the
 * messages of a process: the process writes them itself, so the tail of the
 * log survives a crash of that process and the logs of two processes of one
 * run never interleave in the same file.
 *
 * The class owns a raw file handle only. That keeps it usable from the crash
 * handler of the process (see `CrashReport.hpp`), which must not allocate
 * memory, take a lock or call the runtime.
 */
class LogFile
{
public:
    /**
     * @brief Build the name of the log file of one process.
     *
     * The name is `<program>.<time utc>.<pid>.log`: the name of the executable
     * without its directory and without its extension, the UTC time in the
     * form `YYYYMMDDTHHMMSSZ`, the process id and the extension `log`,
     * separated by dots. The time is part of the name because the process id
     * of an earlier process can be reused.
     *
     * @param[in] image_path Path of the executable of the process.
     * @param[in] utc UTC time the process started at.
     * @param[in] pid Process id of the process.
     * @return The file name without a directory.
     */
    static std::wstring FileNameOf(const std::wstring& image_path, const SYSTEMTIME& utc, unsigned long pid);

    /**
     * @brief Open the log file of the current process.
     *
     * The directories of the path are created when they are missing, so a run
     * whose log directory was removed while it was starting still gets its
     * log. A file which cannot be created is not fatal: the process runs
     * without a log file and every later write is dropped.
     *
     * @param[in] dir Directory the log file is created in.
     * @param[in] image_path Path of the executable of the process.
     * @return true when the log file is open.
     */
    static bool Open(const std::wstring& dir, const std::wstring& image_path);

    /**
     * @brief Close the log file, the process keeps running without one.
     */
    static void Close();

    /**
     * @brief Whether a log file is open.
     * @return true when a log file is open.
     */
    static bool IsOpen();

    /**
     * @brief Append one line to the log file.
     *
     * The text and the line break are written with a single write, so the
     * lines of the threads of the process never interleave. The function
     * allocates the line, which makes it unusable from the crash handler;
     * `Write()` is the variant which does not allocate.
     *
     * @param[in] text Text of the line, without a line break.
     */
    static void WriteLine(const std::string& text);

    /**
     * @brief Append bytes to the log file.
     *
     * The function neither allocates memory nor takes a lock and it never
     * throws, so the crash handler of the process can report through it. A
     * failed write is dropped: a log must never fail the caller.
     *
     * @param[in] data Bytes to write.
     * @param[in] size Number of the bytes.
     */
    static void Write(const char* data, std::size_t size);

    /**
     * @brief Path of the log file.
     * @return The path, empty while no log file is open.
     */
    static const std::wstring& Path();

private:
    LogFile() = delete;

    /** Handle of the log file, `INVALID_HANDLE_VALUE` while no file is open. */
    static std::atomic<HANDLE> s_file;

    /** Path of the log file, empty while no file is open. */
    static std::wstring s_path;
};

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_LOG_FILE_HPP
