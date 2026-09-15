#include <gtest/gtest.h>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <vector>
#include "ModuleTable.hpp"

namespace
{

/**
 * @brief Number of calls of a fake module entry.
 */
struct CallCounters
{
    int init = 0;
    int exit = 0;
};

/**
 * @brief Upper bound of the exit calls of a single fake module.
 *
 * A rollback must call every module at most once. A larger number of calls
 * means that the rollback loop does not terminate; the fake entry throws in
 * that case so the test fails instead of hanging the whole test run.
 */
constexpr int kMaxExitCalls = 8;

/* Failure code used to make a fake module fail. */
const NTSTATUS kFailure = static_cast<NTSTATUS>(0xC0000001L); /* STATUS_UNSUCCESSFUL */

CallCounters g_module_a;
CallCounters g_module_b;
CallCounters g_module_c;

NTSTATUS g_status_a = 0;
NTSTATUS g_status_b = 0;
NTSTATUS g_status_c = 0;

/* Order in which the modules were deinitialized. */
std::vector<int> g_exit_order;

/**
 * @brief Guard against a non terminating rollback loop.
 * @param[in] calls Number of exit calls of the module so far.
 */
void CheckExitBound(int calls)
{
    if (calls > kMaxExitCalls)
    {
        throw std::runtime_error("module exit called too many times");
    }
}

NTSTATUS InitA()
{
    ++g_module_a.init;
    return g_status_a;
}

void ExitA()
{
    CheckExitBound(++g_module_a.exit);
    g_exit_order.push_back(1);
}

NTSTATUS InitB()
{
    ++g_module_b.init;
    return g_status_b;
}

void ExitB()
{
    CheckExitBound(++g_module_b.exit);
    g_exit_order.push_back(2);
}

NTSTATUS InitC()
{
    ++g_module_c.init;
    return g_status_c;
}

void ExitC()
{
    CheckExitBound(++g_module_c.exit);
    g_exit_order.push_back(3);
}

/**
 * @brief Reset the fake module state before every test case.
 */
void ResetFixture()
{
    g_module_a = CallCounters();
    g_module_b = CallCounters();
    g_module_c = CallCounters();
    g_status_a = 0;
    g_status_b = 0;
    g_status_c = 0;
    g_exit_order.clear();
}

const appbox::ModuleInitializer kModules[] = {
    { InitA, ExitA },
    { InitB, ExitB },
    { InitC, ExitC },
};

} // namespace

/**
 * @brief Every module initializes successfully, nothing is rolled back.
 */
TEST(UnitModuleTable, InitAllModulesSuccess)
{
    ResetFixture();

    EXPECT_TRUE(appbox::InitModuleTable(kModules, std::size(kModules)));

    EXPECT_EQ(g_module_a.init, 1);
    EXPECT_EQ(g_module_b.init, 1);
    EXPECT_EQ(g_module_c.init, 1);
    EXPECT_EQ(g_module_a.exit, 0);
    EXPECT_EQ(g_module_b.exit, 0);
    EXPECT_EQ(g_module_c.exit, 0);
}

/**
 * @brief The second module fails: the first one is rolled back exactly once,
 *        the failing module and the modules behind it are not touched.
 */
TEST(UnitModuleTable, InitRollsBackInitializedModulesOnce)
{
    ResetFixture();
    g_status_b = kFailure;

    bool result = true;
    ASSERT_NO_THROW(result = appbox::InitModuleTable(kModules, std::size(kModules)));

    EXPECT_FALSE(result);
    EXPECT_EQ(g_module_a.init, 1);
    EXPECT_EQ(g_module_b.init, 1);
    EXPECT_EQ(g_module_c.init, 0); /* Not reached. */
    EXPECT_EQ(g_module_a.exit, 1); /* Rolled back exactly once. */
    EXPECT_EQ(g_module_b.exit, 0); /* The failing module is not rolled back. */
    EXPECT_EQ(g_module_c.exit, 0);
}

/**
 * @brief The first module fails: nothing has to be rolled back.
 */
TEST(UnitModuleTable, InitFirstModuleFailureDoesNotRollBack)
{
    ResetFixture();
    g_status_a = kFailure;

    bool result = true;
    ASSERT_NO_THROW(result = appbox::InitModuleTable(kModules, std::size(kModules)));

    EXPECT_FALSE(result);
    EXPECT_EQ(g_module_a.init, 1);
    EXPECT_EQ(g_module_b.init, 0);
    EXPECT_EQ(g_module_c.init, 0);
    EXPECT_EQ(g_module_a.exit, 0);
    EXPECT_EQ(g_module_b.exit, 0);
    EXPECT_EQ(g_module_c.exit, 0);
}

/**
 * @brief The last module fails: every module before it is rolled back once, in
 *        reverse order.
 */
TEST(UnitModuleTable, InitLastModuleFailureRollsBackAllPrevious)
{
    ResetFixture();
    g_status_c = kFailure;

    bool result = true;
    ASSERT_NO_THROW(result = appbox::InitModuleTable(kModules, std::size(kModules)));

    EXPECT_FALSE(result);
    EXPECT_EQ(g_module_a.init, 1);
    EXPECT_EQ(g_module_b.init, 1);
    EXPECT_EQ(g_module_c.init, 1);
    EXPECT_EQ(g_module_a.exit, 1);
    EXPECT_EQ(g_module_b.exit, 1);
    EXPECT_EQ(g_module_c.exit, 0);

    const std::vector<int> expected = { 2, 1 }; /* Reverse order. */
    EXPECT_EQ(g_exit_order, expected);
}

/**
 * @brief Every module is deinitialized exactly once, in reverse order.
 */
TEST(UnitModuleTable, ExitDeinitializesInReverseOrder)
{
    ResetFixture();

    appbox::ExitModuleTable(kModules, std::size(kModules));

    EXPECT_EQ(g_module_a.exit, 1);
    EXPECT_EQ(g_module_b.exit, 1);
    EXPECT_EQ(g_module_c.exit, 1);

    const std::vector<int> expected = { 3, 2, 1 };
    EXPECT_EQ(g_exit_order, expected);
}

/**
 * @brief An empty module table is a no-op.
 */
TEST(UnitModuleTable, EmptyTable)
{
    ResetFixture();

    EXPECT_TRUE(appbox::InitModuleTable(kModules, 0));
    appbox::ExitModuleTable(kModules, 0);

    EXPECT_EQ(g_module_a.init, 0);
    EXPECT_EQ(g_module_a.exit, 0);
}
