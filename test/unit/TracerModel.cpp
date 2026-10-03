#include <gtest/gtest.h>
#include "src/core/TracerModel.hpp"
#include "tracer/TracedModules.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Report whether a list holds a row with the given name.
 *
 * @param[in] entries Rows to search.
 * @param[in] name Name to look for.
 * @return Whether the name is a row.
 */
bool HasName(const std::vector<appbox::TracerEntry>& entries, const std::wstring& name)
{
    return std::any_of(entries.begin(), entries.end(),
                       [&name](const appbox::TracerEntry& entry) { return entry.name == name; });
}

} // namespace

/**
 * @brief Both views are built from the parsed modules: the scope from the built
 *        in table of the isolation domains (with the `Zw` aliases a run
 *        reports) and the all exports view from every executable export, so it
 *        carries the Win32 names the scope leaves out.
 */
TEST(Unit_TracerModel, BothViewsAreBuiltFromTheModules)
{
    const std::filesystem::path directory = appbox::tracer::SystemDirectoryForMachine(0);

    std::vector<appbox::TracerEntry> scope;
    std::string                      error;
    ASSERT_TRUE(appbox::BuildTracerView(appbox::TracerView::Scope, directory, scope, error)) << error;
    ASSERT_FALSE(scope.empty());

    for (const auto& entry : scope)
    {
        const std::size_t bang = entry.name.find(L'!');
        ASSERT_NE(bang, std::wstring::npos) << entry.name;

        const std::wstring module = entry.name.substr(0, bang);
        EXPECT_TRUE(module == L"ntdll" || module == L"ws2_32" || module == L"dnsapi") << entry.name;
    }

    EXPECT_TRUE(HasName(scope, L"ntdll!NtCreateFile"));
    /* The alias proves the rows are the names a run reports, not the table names. */
    EXPECT_TRUE(HasName(scope, L"ntdll!ZwClose"));

    std::vector<appbox::TracerEntry> all_exports;
    ASSERT_TRUE(appbox::BuildTracerView(appbox::TracerView::AllExports, directory, all_exports, error)) << error;
    EXPECT_GT(all_exports.size(), scope.size());
    EXPECT_TRUE(HasName(all_exports, L"kernel32!CreateFileW"));
    EXPECT_TRUE(HasName(all_exports, L"kernel32!GetCommandLineW"));
    EXPECT_TRUE(HasName(all_exports, L"kernelbase!CreateFileW"));
}

/**
 * @brief The rows of a view are sorted ASCII ascending, which puts the modules
 *        of the scope in the order dnsapi, ntdll, ws2_32.
 */
TEST(Unit_TracerModel, RowsAreSortedASCII)
{
    std::vector<appbox::TracerEntry> entries;
    std::string                      error;
    ASSERT_TRUE(appbox::BuildTracerView(appbox::TracerView::Scope, appbox::tracer::SystemDirectoryForMachine(0),
                                        entries, error))
        << error;
    ASSERT_FALSE(entries.empty());

    EXPECT_TRUE(std::is_sorted(
        entries.begin(), entries.end(),
        [](const appbox::TracerEntry& left, const appbox::TracerEntry& right) { return left.name < right.name; }));

    EXPECT_EQ(entries.front().name.compare(0, 7, L"dnsapi!"), 0);
    EXPECT_EQ(entries.back().name.compare(0, 7, L"ws2_32!"), 0);
}

/**
 * @brief A directory which holds no module is reported instead of throwing, and
 *        it leaves the rows empty.
 */
TEST(Unit_TracerModel, AFailingDirectoryIsReported)
{
    std::vector<appbox::TracerEntry> entries = {
        { L"ntdll!NtClose", true }
    };
    std::string error;

    EXPECT_FALSE(
        appbox::BuildTracerView(appbox::TracerView::Scope, L"Z:\\appbox\\no\\such\\directory", entries, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(entries.empty());
}

/**
 * @brief The used rows are moved in front of the unused ones, and each group
 *        stays ASCII ascending.
 */
TEST(Unit_TracerModel, UsedRowsComeFirst)
{
    std::vector<appbox::TracerEntry> entries = {
        { L"ntdll!NtClose",         false },
        { L"ntdll!NtCreateFile",    false },
        { L"ntdll!NtCreateSection", false },
        { L"ntdll!ZwClose",         false },
        { L"ws2_32!GetAddrInfoW",   false },
    };

    appbox::MarkTracerEntries(entries, { L"ntdll!ZwClose", L"ntdll!NtCreateFile" });
    appbox::OrderTracerEntries(entries);

    ASSERT_EQ(entries.size(), 5U);
    EXPECT_EQ(entries[0].name, L"ntdll!NtCreateFile");
    EXPECT_EQ(entries[1].name, L"ntdll!ZwClose");
    EXPECT_EQ(entries[2].name, L"ntdll!NtClose");
    EXPECT_EQ(entries[3].name, L"ntdll!NtCreateSection");
    EXPECT_EQ(entries[4].name, L"ws2_32!GetAddrInfoW");

    EXPECT_TRUE(entries[0].used);
    EXPECT_TRUE(entries[1].used);
    EXPECT_FALSE(entries[2].used);
    EXPECT_FALSE(entries[3].used);
    EXPECT_FALSE(entries[4].used);
}

/**
 * @brief The JSON export holds the facts of the run and exactly the functions
 *        the run used, in display order; a path which can not be written is
 *        reported.
 */
TEST(Unit_TracerModel, ExportListsTheUsedFunctions)
{
    const std::vector<appbox::TracerEntry> entries = {
        { L"ntdll!NtClose",         true  },
        { L"ntdll!NtCreateFile",    true  },
        { L"ntdll!NtCreateSection", false },
        { L"ntdll!ZwClose",         true  },
    };

    appbox::tracer::TraceResult result;
    result.status = appbox::tracer::RunStatus::Completed;
    result.processes = 2;
    result.breakpoints = 120;
    result.names = { L"ntdll!NtClose", L"ntdll!NtCreateFile", L"ntdll!ZwClose", L"ntdll!NtCreateSection" };

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "appbox-tracer-export.json";
    std::string                 error;
    ASSERT_TRUE(appbox::SaveTracerExport(path.wstring(), L"C:\\Windows\\System32\\cmd.exe",
                                         appbox::TracerView::AllExports, result, entries, error))
        << error;

    std::ifstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();
    std::filesystem::remove(path);

    const nlohmann::json document = nlohmann::json::parse(text);
    EXPECT_EQ(document.at("program").get<std::string>(), "C:\\Windows\\System32\\cmd.exe");
    EXPECT_EQ(document.at("view").get<std::string>(), "all-exports");
    EXPECT_EQ(document.at("result").get<std::string>(), "completed");
    EXPECT_EQ(document.at("processes").get<std::size_t>(), 2U);
    EXPECT_EQ(document.at("breakpoints").get<std::size_t>(), 120U);
    EXPECT_EQ(document.at("functions"),
              nlohmann::json::array({ "ntdll!NtClose", "ntdll!NtCreateFile", "ntdll!ZwClose" }));

    std::string failing;
    EXPECT_FALSE(appbox::SaveTracerExport(L"Z:\\appbox\\no\\such\\directory\\export.json", L"cmd.exe",
                                          appbox::TracerView::Scope, result, entries, failing));
    EXPECT_FALSE(failing.empty());
}
