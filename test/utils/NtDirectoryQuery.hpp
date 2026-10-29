#ifndef APPBOX_TEST_UTILS_NTDIRECTORYQUERY_HPP
#define APPBOX_TEST_UTILS_NTDIRECTORYQUERY_HPP

#include "WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief One enumeration of a directory with one information class.
 *
 * The fields mirror the request of the `ListDirNt` probe: the probe turns its
 * request into this structure, so the two sides stay the same operation, while
 * a case which asks the file system itself builds the structure directly. A
 * request may name the information class of the query, may ask for a handle of
 * `CreateFileW` instead of one of `NtOpenFile`, may ask for a handle the sandbox
 * did not open, which is built by duplicating the handle and closing the
 * original, and may remove the directory before the query, which is the state of
 * a handle whose object the view no longer holds.
 */
struct NtDirectoryQuery
{
    std::wstring path;                        /* Win32 path of the directory. */
    std::string  info_class;                  /* Name of the class, empty for FileFullDirectoryInformation. */
    bool         extended = false;            /* Query with NtQueryDirectoryFileEx. */
    bool         create_file = false;         /* Open the directory with CreateFileW. */
    bool         duplicate = false;           /* Enumerate a duplicate of the handle. */
    bool         remove_before_query = false; /* Remove the directory before the query. */
};

/**
 * @brief Answer of one enumeration of a directory.
 */
struct NtDirectoryQueryResult
{
    long                     status = 0; /* NTSTATUS of the enumeration. */
    std::vector<std::string> names;      /* Names the query reported, in UTF-8 and in name order. */
};

/**
 * @brief Enumerate a directory with one information class.
 *
 * The function runs in the process which calls it: the `ListDirNt` probe calls
 * it inside the sandbox, where the enumeration reaches the merged view of the
 * layers, while a case calls it in the process of the test executable, which the
 * sandbox is not injected into, where the answer is the one the file system
 * gives for the path itself. That is what a case which checks the feature set of
 * a layer needs, because the answer of the view cannot separate the class a
 * layer refuses from the class the merge reads.
 *
 * The directory is opened with `NtOpenFile`, which registers the handle with the
 * sandbox, or with `CreateFileW` when the query asks for it, which is the second
 * way a directory handle reaches the sandbox. The caller may ask for a handle
 * the sandbox did not open, which is the handle a process inherits or duplicates
 * from another process, and for a directory which is removed before the query.
 *
 * The class is read with the own table of this function instead of the one of
 * the sandbox: a case compares the answer with the names it created itself, so a
 * class the sandbox reads with another layout than the file system wrote shows
 * up as a failure of the case, whatever the sandbox believes about the class. A
 * class which carries no name of an entry reports the status of the call alone.
 *
 * @param[in] query The enumeration to run.
 * @return The status of the enumeration and the names it reported.
 */
NtDirectoryQueryResult QueryNtDirectory(const NtDirectoryQuery& query);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_NTDIRECTORYQUERY_HPP
