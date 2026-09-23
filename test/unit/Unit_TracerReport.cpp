#include <gtest/gtest.h>
#include "tracer/Console.hpp"
#include "tracer/TraceReport.hpp"
#include "tracer/TracedModules.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Build a report header which describes a completed run.
 *
 * @return The header.
 */
appbox::tracer::TraceReportHeader CompletedHeader()
{
    appbox::tracer::TraceReportHeader header;
    header.program = L"C:\\Windows\\System32\\cmd.exe";
    header.debugger = L"C:\\dbg\\cdb.exe";
    header.scope = L"file, registry, network";
    header.processes = 2;
    header.breakpoints = 515;
    header.status = L"completed";
    return header;
}

/**
 * @brief Report whether a text contains a fragment.
 *
 * @param[in] text Text to search in.
 * @param[in] fragment Fragment to search for.
 * @return Whether the fragment occurs in the text.
 */
bool Contains(const std::wstring& text, const std::wstring& fragment)
{
    return text.find(fragment) != std::wstring::npos;
}

} // namespace

/**
 * @brief The header of the report carries the facts of the run, so a report can
 *        be interpreted without the console output of the run.
 */
TEST(TracerReport, TheHeaderDescribesTheRun)
{
    const std::wstring text =
        appbox::tracer::FormatReport(CompletedHeader(), {L"ntdll!NtClose"}, false);

    EXPECT_TRUE(Contains(text, L"AppBoxTracer report"));
    EXPECT_TRUE(Contains(text, L"program     : C:\\Windows\\System32\\cmd.exe"));
    EXPECT_TRUE(Contains(text, L"debugger    : C:\\dbg\\cdb.exe"));
    EXPECT_TRUE(Contains(text, L"scope       : file, registry, network"));
    EXPECT_TRUE(Contains(text, L"processes   : 2"));
    EXPECT_TRUE(Contains(text, L"breakpoints : 515 per process"));
    EXPECT_TRUE(Contains(text, L"result      : completed"));
}

/**
 * @brief The functions are grouped by the module which exports them, sorted and
 *        without duplicates.
 */
TEST(TracerReport, FunctionsAreGroupedSortedAndUnique)
{
    const std::vector<std::wstring> names = {L"ntdll!NtOpenKey",
                                             L"kernel32!CreateFileW",
                                             L"ntdll!NtClose",
                                             L"kernel32!CreateFileW"};

    const std::wstring text = appbox::tracer::FormatReport(CompletedHeader(), names, false);

    EXPECT_TRUE(Contains(text, L"kernel32.dll (1 functions)"));
    EXPECT_TRUE(Contains(text, L"ntdll.dll (2 functions)"));

    /* kernel32 comes before ntdll, and the names inside a module are sorted. */
    const std::size_t kernel32 = text.find(L"kernel32.dll");
    const std::size_t ntdll = text.find(L"ntdll.dll");
    ASSERT_NE(kernel32, std::wstring::npos);
    ASSERT_NE(ntdll, std::wstring::npos);
    EXPECT_LT(kernel32, ntdll);

    const std::size_t close = text.find(L"  ntdll!NtClose\n");
    const std::size_t open = text.find(L"  ntdll!NtOpenKey\n");
    ASSERT_NE(close, std::wstring::npos);
    ASSERT_NE(open, std::wstring::npos);
    EXPECT_LT(close, open);
}

/**
 * @brief Every name of an address is reported, because a call can reach the
 *        same code through several names.
 */
TEST(TracerReport, EveryAliasOfAnAddressIsReported)
{
    const std::vector<std::wstring> names = {L"ntdll!NtClose",
                                             L"ntdll!ZwClose",
                                             L"kernel32!HeapAlloc",
                                             L"kernelbase!HeapAlloc"};

    const std::wstring text = appbox::tracer::FormatReport(CompletedHeader(), names, false);

    EXPECT_TRUE(Contains(text, L"  ntdll!NtClose\n"));
    EXPECT_TRUE(Contains(text, L"  ntdll!ZwClose\n"));
    EXPECT_TRUE(Contains(text, L"  kernel32!HeapAlloc\n"));
    EXPECT_TRUE(Contains(text, L"  kernelbase!HeapAlloc\n"));
}

/**
 * @brief With the category annotation every function shows why it is in the
 *        scope, which is what makes the report usable for the isolation work.
 */
TEST(TracerReport, CategoriesCanBeAnnotated)
{
    const std::vector<std::wstring> names = {L"ntdll!NtDeviceIoControlFile", L"ntdll!NtOpenKey"};

    const std::wstring text = appbox::tracer::FormatReport(CompletedHeader(), names, true);

    EXPECT_TRUE(Contains(text, L"  ntdll!NtDeviceIoControlFile  [file, network]\n"));
    EXPECT_TRUE(Contains(text, L"  ntdll!NtOpenKey  [registry]\n"));
}

/**
 * @brief A run which used nothing still produces a readable report.
 */
TEST(TracerReport, AnEmptyResultIsReported)
{
    const std::wstring text = appbox::tracer::FormatReport(CompletedHeader(), {}, false);

    EXPECT_TRUE(Contains(text, L"no function of the scope was used"));
}

/**
 * @brief A run which was aborted says so in the header instead of looking like a
 *        complete one.
 */
TEST(TracerReport, AnAbortedRunIsMarked)
{
    appbox::tracer::TraceReportHeader header = CompletedHeader();
    header.status = L"aborted: the time limit of 600 seconds was reached";

    const std::wstring text = appbox::tracer::FormatReport(header, {L"ntdll!NtClose"}, false);

    EXPECT_TRUE(Contains(text, L"result      : aborted: the time limit of 600 seconds was reached"));
}

/**
 * @brief The module name of an image path is the lowercased file name without
 *        its extension, which is the key the breakpoint addresses use.
 */
TEST(TracerReport, ModuleNamesComeFromTheImagePath)
{
    EXPECT_EQ(appbox::tracer::ModuleNameFromImagePath(L"C:\\Windows\\System32\\KERNEL32.DLL"),
              L"kernel32");
    EXPECT_EQ(appbox::tracer::ModuleNameFromImagePath(L"ntdll.dll"), L"ntdll");
    EXPECT_EQ(appbox::tracer::ModuleNameFromImagePath(L"cmd.exe"), L"cmd");
    EXPECT_EQ(appbox::tracer::ModuleNameFromImagePath(L"C:\\dir\\name.without.extension"),
              L"name.without");
    EXPECT_EQ(appbox::tracer::ModuleNameFromImagePath(L"KernelBase"), L"kernelbase");
    EXPECT_TRUE(appbox::tracer::ModuleNameFromImagePath(L"").empty());
}

/**
 * @brief The raw output of a debugger is decoded from the code page of the
 *        console before it is stored as UTF-8.
 */
TEST(TracerReport, ConsoleBytesAreDecoded)
{
    EXPECT_EQ(appbox::tracer::DecodeConsoleBytes("ModLoad: cmd.exe\r\n"), L"ModLoad: cmd.exe\r\n");
    EXPECT_TRUE(appbox::tracer::DecodeConsoleBytes("").empty());
}
