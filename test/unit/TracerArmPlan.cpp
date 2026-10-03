#include <gtest/gtest.h>
#include "tracer/ArmPlan.hpp"
#include "tracer/TracedModules.hpp"
#include <windows.h>
#include <filesystem>
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
    return appbox::tracer::LoadTracedModules(appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));
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
const appbox::tracer::ArmGroup* FindGroup(const std::vector<appbox::tracer::ArmGroup>& plan, const std::wstring& name)
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
TEST(Unit_TracerTracedModules, TheSystemModulesAreReadable)
{
    const auto modules = SystemModules();

    ASSERT_EQ(modules.size(), 5U);
    EXPECT_NE(modules.find(L"ntdll"), modules.end());
    EXPECT_NE(modules.find(L"kernel32"), modules.end());
    EXPECT_NE(modules.find(L"kernelbase"), modules.end());
    EXPECT_NE(modules.find(L"ws2_32"), modules.end());
    EXPECT_NE(modules.find(L"dnsapi"), modules.end());

    for (const auto& module : modules)
    {
        EXPECT_FALSE(module.second.path.empty());
        EXPECT_GT(module.second.image.Exports().size(), 100U);
    }
}

/**
 * @brief A 32 bit target runs against the WOW64 copies, which have their own
 *        export tables.
 */
TEST(Unit_TracerTracedModules, AThirtyTwoBitTargetUsesTheWow64Directory)
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
TEST(Unit_TracerArmPlan, TheCategoryScopeIsSmallerThanEveryExport)
{
    const auto modules = SystemModules();
    ASSERT_EQ(modules.size(), 5U);

    const std::filesystem::path directory = appbox::tracer::SystemDirectoryForMachine(kMachineAmd64);
    const auto categories = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false, directory);
    const auto every_export = appbox::tracer::BuildArmPlan(modules, {}, true, directory);

    /* The measured sizes are about 120 addresses for the categories and more
     * than 5000 for every export, so the test pins the order of magnitude only. */
    EXPECT_GT(categories.size(), 100U);
    EXPECT_LT(categories.size(), every_export.size());
    EXPECT_GT(every_export.size(), 3000U);
}

/**
 * @brief The plan is sorted by module and address, and every planned address is
 *        executable, so a trap byte can never land in data.
 */
TEST(Unit_TracerArmPlan, ThePlanIsSortedAndExecutable)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    ASSERT_FALSE(plan.empty());

    std::wstring  previous_module;
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
 * @brief The lowest level entry points of the three isolation domains are
 *        planned, the Win32 wrappers and unrelated functions are not.
 */
TEST(Unit_TracerArmPlan, TheIsolationDomainsArePlanned)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtCreateFile"));
    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtOpenFile"));
    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtOpenKey"));
    EXPECT_TRUE(ContainsName(plan, L"ntdll!NtCreateNamedPipeFile"));
    EXPECT_TRUE(ContainsName(plan, L"ws2_32!GetAddrInfoW"));
    EXPECT_TRUE(ContainsName(plan, L"dnsapi!DnsQuery_W"));

    /* The Win32 wrappers are not part of the default scope. */
    EXPECT_FALSE(ContainsName(plan, L"kernel32!CreateFileW"));
    EXPECT_FALSE(ContainsName(plan, L"kernelbase!CreateFileW"));
    EXPECT_FALSE(ContainsName(plan, L"kernelbase!RegOpenKeyExW"));
    EXPECT_FALSE(ContainsName(plan, L"kernel32!CreateNamedPipeW"));

    EXPECT_FALSE(ContainsName(plan, L"ntdll!NtAllocateVirtualMemory"));
    EXPECT_FALSE(ContainsName(plan, L"ntdll!RtlAllocateHeap"));
}

/**
 * @brief Several names of one function share one breakpoint, which is what the
 *        alias grouping is for: NtClose and ZwClose are the same function.
 */
TEST(Unit_TracerArmPlan, NamesOfOneFunctionShareOneBreakpoint)
{
    const auto modules = SystemModules();
    const auto plan = appbox::tracer::BuildArmPlan(modules, appbox::tracer::AllCategories(), false,
                                                   appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

    const auto* group = FindGroup(plan, L"ntdll!NtClose");
    ASSERT_NE(group, nullptr);
    EXPECT_NE(std::find(group->names.begin(), group->names.end(), L"ntdll!ZwClose"), group->names.end());
    EXPECT_GE(group->names.size(), 2U);
}

/**
 * @brief A forwarded export has no code of its own, so it is planned at the
 *        address of the function which implements it. The exhaustive scope
 *        contains forwarded names of kernel32, which proves that the chain is
 *        followed.
 */
TEST(Unit_TracerArmPlan, ForwardedExportsAreResolvedToTheirImplementation)
{
    const auto modules = SystemModules();
    const auto plan =
        appbox::tracer::BuildArmPlan(modules, {}, true, appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));

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
TEST(Unit_TracerArmPlan, ArmLinesCarryTheAddressAndTheMarker)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        { L"kernel32", 0x1234U, { L"kernel32!CreateFileW" }            },
        { L"ntdll",    0x10U,   { L"ntdll!NtClose", L"ntdll!ZwClose" } },
    };
    const appbox::tracer::ModuleBases bases = {
        { L"kernel32", 0x10000000ULL },
        { L"ntdll",    0x20000000ULL }
    };

    const auto lines = appbox::tracer::BuildArmLines(plan, bases);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "bp /1 0x10001234 \".echo APPBOXHIT kernel32!CreateFileW; g\"");
    EXPECT_EQ(lines[1], "bp /1 0x20000010 \".echo APPBOXHIT ntdll!NtClose ntdll!ZwClose; g\"");
}

/**
 * @brief A module whose base is unknown in this session is skipped instead of
 *        producing a line with a wrong address.
 */
TEST(Unit_TracerArmPlan, GroupsWithoutABaseAreSkipped)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        { L"kernel32", 0x10U, { L"kernel32!CreateFileW" } },
        { L"ntdll",    0x20U, { L"ntdll!NtClose" }        },
    };
    const appbox::tracer::ModuleBases bases = {
        { L"ntdll", 0x20000000ULL }
    };

    const auto lines = appbox::tracer::BuildArmLines(plan, bases);

    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0], "bp /1 0x20000020 \".echo APPBOXHIT ntdll!NtClose; g\"");
}

/**
 * @brief The modules of a plan which are not loaded yet are the ones a session
 *        has to wait for: they are what the load filter is built from.
 */
TEST(Unit_TracerArmPlan, TheModulesWhichAreNotLoadedYetArePending)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        { L"ntdll",  0x10U, { L"ntdll!NtCreateFile" }  },
        { L"ws2_32", 0x20U, { L"ws2_32!GetAddrInfoW" } },
        { L"dnsapi", 0x30U, { L"dnsapi!DnsQuery_W" }   },
    };
    const appbox::tracer::ModuleBases bases = {
        { L"ntdll",    0x1000ULL },
        { L"kernel32", 0x2000ULL }
    };

    const auto pending = appbox::tracer::SelectPendingModules(plan, bases);

    ASSERT_EQ(pending.size(), 2U);
    EXPECT_EQ(pending[0], L"dnsapi");
    EXPECT_EQ(pending[1], L"ws2_32");
    EXPECT_TRUE(appbox::tracer::SelectPendingModules({}, bases).empty());
}

/**
 * @brief A session arms the groups whose module has a known base address and
 *        which it has not armed yet, which is how a module that is loaded later
 *        joins the plan of a running process.
 */
TEST(Unit_TracerArmPlan, ASessionArmsWhatItHasNotArmedYet)
{
    const std::vector<appbox::tracer::ArmGroup> plan = {
        { L"ntdll",  0x10U, { L"ntdll!NtCreateFile" }  },
        { L"ws2_32", 0x20U, { L"ws2_32!GetAddrInfoW" } },
        { L"dnsapi", 0x30U, { L"dnsapi!DnsQuery_W" }   },
    };
    const appbox::tracer::ModuleBases bases = {
        { L"ntdll",  0x1000ULL },
        { L"ws2_32", 0x2000ULL }
    };

    /* The first arm of a session takes every group whose base is known. */
    const auto first = appbox::tracer::SelectArmable(plan, bases, {});
    ASSERT_EQ(first.size(), 2U);
    EXPECT_EQ(first[0].module, L"ntdll");
    EXPECT_EQ(first[1].module, L"ws2_32");

    /* A module which is not loaded can not be armed. */
    const std::set<std::wstring> armed = { L"ntdll", L"ws2_32" };
    EXPECT_TRUE(appbox::tracer::SelectArmable(plan, bases, armed).empty());

    /* Once the loader mapped it, only the missing module is armed. */
    const appbox::tracer::ModuleBases loaded = {
        { L"ntdll",  0x1000ULL },
        { L"ws2_32", 0x2000ULL },
        { L"dnsapi", 0x3000ULL }
    };
    const auto second = appbox::tracer::SelectArmable(plan, loaded, armed);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second[0].module, L"dnsapi");
}

/**
 * @brief The load filter is one debugger command per module, which is what makes
 *        a module that is loaded on demand observable at all.
 */
TEST(Unit_TracerArmPlan, LoadFiltersAreOneCommandPerModule)
{
    const auto lines = appbox::tracer::BuildLoadFilterLines({ L"ws2_32", L"dnsapi" });

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "sxe ld:ws2_32");
    EXPECT_EQ(lines[1], "sxe ld:dnsapi");
    EXPECT_TRUE(appbox::tracer::BuildLoadFilterLines({}).empty());
}
