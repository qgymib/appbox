#include <gtest/gtest.h>
#include "tracer/ArmPlan.hpp"
#include "tracer/TracedModules.hpp"
#include "tracer/TraceReport.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

/** Machine type of an x64 image (IMAGE_FILE_MACHINE_AMD64). */
constexpr std::uint16_t kMachineAmd64 = 0x8664;

/**
 * @brief Load the traced modules of the 64 bit system directory.
 *
 * @return The parsed modules, empty when the system DLLs can not be read.
 */
appbox::tracer::ModuleImages SystemModules()
{
    return appbox::tracer::LoadTracedModules(
        appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));
}

/**
 * @brief Report whether the plan contains a name.
 *
 * @param[in] plan Plan to search.
 * @param[in] name `module!function` name to look for.
 * @return Whether the name is part of the plan.
 */
bool ContainsName(const std::vector<appbox::tracer::ArmGroup>& plan, const std::wstring& name)
{
    for (const auto& group : plan)
    {
        if (std::find(group.names.begin(), group.names.end(), name) != group.names.end())
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Find the group which contains a name.
 *
 * @param[in] plan Plan to search.
 * @param[in] name `module!function` name to look for.
 * @return The group, or nullptr when the name is not planned.
 */
const appbox::tracer::ArmGroup* FindGroup(const std::vector<appbox::tracer::ArmGroup>& plan,
                                          const std::wstring& name)
{
    for (const auto& group : plan)
    {
        if (std::find(group.names.begin(), group.names.end(), name) != group.names.end())
        {
            return &group;
        }
    }

    return nullptr;
}

} // namespace

/**
 * @brief The traced modules are read from the system directory and carry their
 *        export tables.
 */
TEST(TracerTracedModules, TheSystemModulesAreReadable)
{
    const auto modules = SystemModules();

    ASSERT_EQ(modules.size(), 3U);
    EXPECT_NE(modules.find(L"ntdll"), modules.end());
    EXPECT_NE(modules.find(L"kernel32"), modules.end());
    EXPECT_NE(modules.find(L"kernelbase"), modules.end());

    for (const auto& module : modules)
    {
        EXPECT_FALSE(module.second.path.empty());
        EXPECT_GT(module.second.image.Exports().size(), 500U);
    }
}

/**
 * @brief A 32 bit target runs against the WOW64 copies, which have their own
 *        export tables.
 */
TEST(TracerTracedModules, AThirtyTwoBitTargetUsesTheWow64Directory)
{
    const std::filesystem::path sixty_four = appbox::tracer::SystemDirectoryForMachine(kMachineAmd64);
    const std::filesystem::path thirty_two = appbox::tracer::SystemDirectoryForMachine(0x014C);

    EXPECT_FALSE(sixty_four.empty());
    EXPECT_FALSE(thirty_two.empty());
    EXPECT_NE(thirty_two.wstring().find(L"WOW64"), std::wstring::npos);
}

/**
 * @brief The category scope arms a part of the export surface, the exhaustive
 *        scope arms all of it.
 */
TEST(TracerArmPlan, TheCategoryScopeIsSmallerThanEveryExport)
{
    const auto modules = SystemModules();
    ASSERT_EQ(modules.size(), 3U);

    const std::filesystem::path directory = appbox::tracer::SystemDirectoryForMachine(kMachineAmd64);
    const auto categories =
        appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false, directory);
    const auto every_export = appbox::tracer::BuildArmPlan(modules, {}, true, directory);

    /* The measured sizes are 583 names for the categories and 5239 addresses for
     * every export, so the test pins the order of magnitude only. */
    EXPECT_GT(categories.size(), 100U);
    EXPECT_LT(categories.size(), every_export.size());
    EXPECT_GT(every_export.size(), 3000U);
}

/**
 * @brief The plan is sorted by module and address, and every planned address is
 *        executable, so a trap byte can never land in data.
 */
TEST(TracerArmPlan, ThePlanIsSortedAndExecutable)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    ASSERT_FALSE(plan.empty());

    std::wstring previous_module;
    std::uint32_t previous_rva = 0;
    for (const auto& group : plan)
    {
        EXPECT_FALSE(group.module.empty());
        EXPECT_FALSE(group.names.empty());
        EXPECT_NE(group.rva, 0U);

        if (group.module == previous_module)
        {
            EXPECT_GT(group.rva, previous_rva) << group.module;
        }
        else
        {
            EXPECT_LT(previous_module, group.module);
        }

        previous_module = group.module;
        previous_rva = group.rva;

        const auto module = modules.find(group.module);
        ASSERT_NE(module, modules.end()) << group.module;
        EXPECT_TRUE(module->second.image.IsExecutable(group.rva)) << group.module;
    }
}

/**
 * @brief The entry points of the three isolation domains are planned, unrelated
 *        functions are not.
 */
TEST(TracerArmPlan, TheIsolationDomainsArePlanned)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtCreateFile"));
    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtOpenKey"));
    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtCreateNamedPipeFile"));
    EXPECT_TRUE(ContainsName(plan, L"kernel32!CreateFileW"));
    EXPECT_FALSE(ContainsName(plan, L"ntdll!NtAllocateVirtualMemory"));
    EXPECT_FALSE(ContainsName(plan, L"ntdll!RtlAllocateHeap"));
}

/**
 * @brief Several names of one function share one breakpoint, which is what the
 *        alias grouping is for: NtClose and ZwClose are the same function.
 */
TEST(TracerArmPlan, NamesOfOneFunctionShareOneBreakpoint)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    const auto* group = FindGroup(plan, L"ntdll!NtClose");
    ASSERT_NE(group, nullptr);
    EXPECT_NE(std::find(group->names.begin(), group->names.end(), L"ntdll!ZwClose"),
              group->names.end());
    EXPECT_GE(group->names.size(), 2U);
}

/**
 * @brief A forwarded export has no code of its own, so it is planned at the
 *        address of the function which implements it. The exhaustive scope
 *        contains forwarded names of kernel32, which proves that the chain is
 *        followed.
 */
TEST(TracerArmPlan, ForwardedExportsAreResolvedToTheirImplementation)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, {}, true,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    bool found_cross_module_group = false;
    for (const auto& group : plan)
    {
        std::set<std::wstring> exporting_modules;
        for (const auto& name : group.names)
        {
            exporting_modules.insert(name.substr(0, name.find(L'!')));
        }

        if (exporting_modules.size() > 1U)
        {
            found_cross_module_group = true;
            break;
        }
    }

    EXPECT_TRUE(found_cross_module_group);
}

/**
 * @brief The generated lines carry the absolute address of the session and the
 *        marker which reports the hit.
 */
TEST(TracerArmPlan, ArmLinesCarryTheAddressAndTheMarker)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        {L"kernel32", 0x1234U, {L"kernel32!CreateFileW"}},
        {L"ntdll", 0x10U, {L"ntdll!NtClose", L"ntdll!ZwClose"}},
    };
    const appbox::tracer::ModuleBases bases = {{L"kernel32", 0x10000000ULL},
                                               {L"ntdll", 0x20000000ULL}};

    const auto lines = appbox::tracer::BuildArmLines(plan, bases);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "bp /1 0x10001234 \".echo APPBOXHIT kernel32!CreateFileW; g\"");
    EXPECT_EQ(lines[1], "bp /1 0x20000010 \".echo APPBOXHIT ntdll!NtClose ntdll!ZwClose; g\"");
}

/**
 * @brief A module whose base is unknown in this session is skipped instead of
 *        producing a line with a wrong address.
 */
TEST(TracerArmPlan, GroupsWithoutABaseAreSkipped)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        {L"kernel32", 0x10U, {L"kernel32!CreateFileW"}},
        {L"ntdll", 0x20U, {L"ntdll!NtClose"}},
    };
    const appbox::tracer::ModuleBases bases = {{L"ntdll", 0x20000000ULL}};

    const auto lines = appbox::tracer::BuildArmLines(plan, bases);

    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0], "bp /1 0x20000020 \".echo APPBOXHIT ntdll!NtClose; g\"");
}

/**
 * @brief The listing of the scope names every planned function, so the scope
 *        patterns can be reviewed before a run.
 */
TEST(TracerArmPlan, ScopeListingShowsEveryName)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        {L"kernelbase",
         0x100U,
         {L"kernel32!GetCommandLineW", L"kernelbase!GetCommandLineW"}},
        {L"ntdll", 0x200U, {L"ntdll!NtCreateFile", L"ntdll!NtOpenKey"}},
    };

    const std::wstring text =
        appbox::tracer::FormatScope(plan, L"file, registry, network", true);

    EXPECT_NE(text.find(L"Scope: file, registry, network"), std::wstring::npos);
    EXPECT_NE(text.find(L"Breakpoints: 2"), std::wstring::npos);
    EXPECT_NE(text.find(L"kernelbase.dll: 1 breakpoints, 2 names"), std::wstring::npos);
    EXPECT_NE(text.find(L"  kernel32!GetCommandLineW\n"), std::wstring::npos);
    EXPECT_NE(text.find(L"  ntdll!NtCreateFile  [file]"), std::wstring::npos);
    EXPECT_NE(text.find(L"  ntdll!NtOpenKey  [registry]"), std::wstring::npos);

    const std::wstring plain = appbox::tracer::FormatScope(plan, L"all exports", false);
    EXPECT_NE(plain.find(L"Scope: all exports"), std::wstring::npos);
    EXPECT_EQ(plain.find(L"[file]"), std::wstring::npos);
    EXPECT_NE(plain.find(L"  ntdll!NtCreateFile\n"), std::wstring::npos);
}

/**
 * @brief The report is written as UTF-8, which keeps the file readable
 *        independently of the code page of the machine.
 */
TEST(TracerArmPlan, ReportsAreWrittenAsUtf8)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (L"appbox-tracer-unit-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");

    const std::wstring text = L"ntdll!NtClose\n\u00e4\u00f6\u00fc\n";
    const std::wstring error = appbox::tracer::WriteUtf8File(path, text);
    EXPECT_TRUE(error.empty()) << error;

    std::ifstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    const std::string bytes((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
    stream.close();
    std::filesystem::remove(path);

    /* The umlauts are two bytes each in UTF-8, and the file is not written with
     * a byte order mark. */
    EXPECT_EQ(bytes.rfind("\xEF\xBB\xBF", 0), std::string::npos);
    EXPECT_NE(bytes.find("ntdll!NtClose\n"), std::string::npos);
    EXPECT_NE(bytes.find("\xC3\xA4\xC3\xB6\xC3\xBC"), std::string::npos);
}

/**
 * @brief A path which can not be written is reported instead of failing
 *        silently.
 */
TEST(TracerArmPlan, AReportWhichCanNotBeWrittenIsReported)
{
    const std::wstring error =
        appbox::tracer::WriteUtf8File(L"Z:\\appbox\\no\\such\\directory\\report.txt", L"text\n");

    EXPECT_FALSE(error.empty());
}
