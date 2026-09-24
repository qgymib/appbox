#ifndef APPBOX_TEST_UTILS_COREDUMP_HPP
#define APPBOX_TEST_UTILS_COREDUMP_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace appbox::test
{

/** Option which turns a process into the coredump writer. */
constexpr const wchar_t* kCoredumpWriterOption = L"appbox-coredump-writer";
/** Option with the process the dump starts at. */
constexpr const wchar_t* kCoredumpPidOption = L"appbox-coredump-pid";
/** Option with the directory the dumps are written to. */
constexpr const wchar_t* kCoredumpDirOption = L"appbox-coredump-dir";
/** Option with the name of the test the dumps belong to. */
constexpr const wchar_t* kCoredumpTagOption = L"appbox-coredump-tag";

/**
 * @brief The work one invocation of the coredump writer has to do.
 */
struct CoredumpRequest
{
    unsigned long        pid = 0;      /* Process the dump starts at. */
    std::filesystem::path dump_dir;    /* Directory the dumps are written to. */
    std::wstring         tag;          /* Name of the test, part of the file names. */
};

/**
 * @brief Run the coredump writer when the command line of the current process asks for it.
 *
 * The writer is a process of its own: `MiniDumpWriteDump` suspends every
 * thread of the process it dumps, which must not be the process which calls
 * it. The function writes one dump per process of the tree below the process
 * of the request and reports every dump on stderr.
 *
 * @return true when the current process is the coredump writer, which means
 *         the caller has to return instead of running the tests.
 */
bool RunCoredumpWriterIfRequested();

/**
 * @brief Read the request of a coredump writer out of a command line.
 * @param[in] command_line The command line of the process.
 * @param[out] request The request; untouched when the command line does not
 *                     hold a complete request.
 * @return true when the command line asks for a coredump writer.
 */
bool ParseCoredumpRequest(const std::wstring& command_line, CoredumpRequest& request);

/**
 * @brief Build the command line which starts the coredump writer of this executable.
 * @param[in] request The work the writer has to do.
 * @return The command line, quoted as `CreateProcessW` expects it.
 */
std::wstring BuildCoredumpWriterCommandLine(const CoredumpRequest& request);

/**
 * @brief Get the path of the running executable.
 * @return The full path of the executable of the current process.
 */
std::wstring GetOwnExecutablePath();

/**
 * @brief Replace every character a file name cannot hold.
 * @param[in] tag The name the dump files start with.
 * @return The tag with the characters outside of `[A-Za-z0-9._-]` replaced.
 */
std::wstring SanitizeCoredumpTag(const std::wstring& tag);

/**
 * @brief Collect a process and its descendants.
 *
 * The processes are read from one snapshot of the process list, so a process
 * which is started afterwards is not part of the result. The current process
 * is never part of the result, which keeps a walk which starts at the current
 * process usable for a termination.
 *
 * @param[in] root_pid The process the walk starts at.
 * @return The process ids, the process of the request first, each of them once.
 */
std::vector<unsigned long> CollectProcessTree(unsigned long root_pid);

/**
 * @brief Write a full memory minidump of one process.
 *
 * The dump holds the full memory, the thread information, the handle table and
 * the unloaded modules, so a debugger can show the variables and the heap of
 * the process. The file is removed again when the dump fails, so a dump file
 * which exists is a usable dump.
 *
 * @param[in] pid Process to dump.
 * @param[in] dump_path Path of the dump file to write.
 * @param[out] error Description of the failure, in UTF-8.
 * @return true on success.
 */
bool WriteCoredump(unsigned long pid, const std::filesystem::path& dump_path, std::string& error);

/**
 * @brief Write one dump per process of the tree of a request.
 * @param[in] request The request which names the process, the directory and
 *                    the tag of the dumps.
 * @param[out] errors Description of every dump which failed, in UTF-8.
 * @return The paths of the dumps which were written.
 */
std::vector<std::filesystem::path> DumpProcessTree(const CoredumpRequest& request, std::vector<std::string>& errors);

/**
 * @brief Terminate every descendant of a process.
 * @param[in] root_pid Process whose descendants are terminated.
 * @param[in] excluded_pid Process which has to survive, 0 for none.
 */
void TerminateProcessTree(unsigned long root_pid, unsigned long excluded_pid);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_COREDUMP_HPP
