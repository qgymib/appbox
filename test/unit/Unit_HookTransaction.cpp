#include "hook/HookTransaction.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace
{

/* Error code returned by a fake Detour operation to simulate a failure. */
constexpr LONG kFakeError = 5;

/* Operations performed by the fake Detour implementation, in order. */
std::vector<std::string> g_ops;

/* Status returned by each fake operation. */
LONG g_status_begin = NO_ERROR;
LONG g_status_update_thread = NO_ERROR;
LONG g_status_commit = NO_ERROR;

/* Hook whose attach or detach fails, null when every hook succeeds. */
void** g_failing_pointer = nullptr;

/* Fake function addresses of the hook table. */
void* g_pointer_a = nullptr;
void* g_pointer_b = nullptr;
void* g_pointer_c = nullptr;
void* g_detour = reinterpret_cast<void*>(0x1000);

LONG FakeBegin()
{
    g_ops.push_back("begin");
    return g_status_begin;
}

LONG FakeUpdateThread(HANDLE)
{
    g_ops.push_back("update_thread");
    return g_status_update_thread;
}

LONG FakeAttach(void** ppPointer, void* pDetour)
{
    (void)pDetour;
    g_ops.push_back("attach");

    return (ppPointer == g_failing_pointer) ? kFakeError : NO_ERROR;
}

LONG FakeDetach(void** ppPointer, void* pDetour)
{
    (void)pDetour;
    g_ops.push_back("detach");

    return (ppPointer == g_failing_pointer) ? kFakeError : NO_ERROR;
}

LONG FakeCommit()
{
    g_ops.push_back("commit");
    return g_status_commit;
}

/**
 * @brief Build the injected Detour operations.
 * @return The operations used by the tests.
 */
appbox::DetourOps MakeFakeOps()
{
    return appbox::DetourOps{ FakeBegin, FakeUpdateThread, FakeAttach, FakeDetach, FakeCommit };
}

/**
 * @brief Reset the fake Detour state before every test case.
 */
void ResetFixture()
{
    g_ops.clear();
    g_status_begin = NO_ERROR;
    g_status_update_thread = NO_ERROR;
    g_status_commit = NO_ERROR;
    g_failing_pointer = nullptr;
}

/* HookC has no detour, it only carries a resolved function address. */
const appbox::HookRecord kHookA = { "HookA", nullptr, &g_pointer_a, g_detour };
const appbox::HookRecord kHookB = { "HookB", nullptr, &g_pointer_b, g_detour };
const appbox::HookRecord kHookC = { "HookC", nullptr, &g_pointer_c, nullptr };

const appbox::HookRecord* const kHooks[] = { &kHookA, &kHookB, &kHookC };

} // namespace

/**
 * @brief Every hook with a detour is attached, hooks without a detour are
 *        skipped, the transaction is committed.
 */
TEST(UnitHookTransaction, AttachSkipsHooksWithoutDetour)
{
    ResetFixture();

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Attach, MakeFakeOps());

    EXPECT_TRUE(result.bSuccess);
    EXPECT_TRUE(result.pFailedHook == nullptr);

    const std::vector<std::string> expected = { "begin", "update_thread", "attach", "attach", "commit" };
    EXPECT_EQ(g_ops, expected);
}

/**
 * @brief Detaching uses the detach operation of the Detours library.
 */
TEST(UnitHookTransaction, DetachUsesDetachOperation)
{
    ResetFixture();

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Detach, MakeFakeOps());

    EXPECT_TRUE(result.bSuccess);

    const std::vector<std::string> expected = { "begin", "update_thread", "detach", "detach", "commit" };
    EXPECT_EQ(g_ops, expected);
}

/**
 * @brief A failing attach is reported together with the name of the hook and
 *        the transaction is still closed.
 */
TEST(UnitHookTransaction, AttachFailureReportsHookName)
{
    ResetFixture();
    g_failing_pointer = &g_pointer_b;

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Attach, MakeFakeOps());

    EXPECT_FALSE(result.bSuccess);
    ASSERT_TRUE(result.pFailedHook != nullptr);
    EXPECT_STREQ(result.pFailedHook, "HookB");
    EXPECT_EQ(result.status, kFakeError);

    const std::vector<std::string> expected = { "begin", "update_thread", "attach", "attach", "commit" };
    EXPECT_EQ(g_ops, expected);
}

/**
 * @brief A failing detach is reported together with the name of the hook.
 */
TEST(UnitHookTransaction, DetachFailureReportsHookName)
{
    ResetFixture();
    g_failing_pointer = &g_pointer_a;

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Detach, MakeFakeOps());

    EXPECT_FALSE(result.bSuccess);
    ASSERT_TRUE(result.pFailedHook != nullptr);
    EXPECT_STREQ(result.pFailedHook, "HookA");
    EXPECT_EQ(result.status, kFakeError);
}

/**
 * @brief When the transaction cannot be opened nothing else is performed.
 */
TEST(UnitHookTransaction, BeginFailureSkipsEverything)
{
    ResetFixture();
    g_status_begin = kFakeError;

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Attach, MakeFakeOps());

    EXPECT_FALSE(result.bSuccess);
    EXPECT_EQ(result.status, kFakeError);

    const std::vector<std::string> expected = { "begin" };
    EXPECT_EQ(g_ops, expected);
}

/**
 * @brief A failing thread registration closes the open transaction and attaches
 *        nothing.
 */
TEST(UnitHookTransaction, UpdateThreadFailureClosesTransaction)
{
    ResetFixture();
    g_status_update_thread = kFakeError;

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Attach, MakeFakeOps());

    EXPECT_FALSE(result.bSuccess);
    EXPECT_EQ(result.status, kFakeError);

    const std::vector<std::string> expected = { "begin", "update_thread", "commit" };
    EXPECT_EQ(g_ops, expected);
}

/**
 * @brief A failing commit is reported as a failure.
 */
TEST(UnitHookTransaction, CommitFailureIsReported)
{
    ResetFixture();
    g_status_commit = kFakeError;

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, std::size(kHooks), appbox::HookAction::Attach, MakeFakeOps());

    EXPECT_FALSE(result.bSuccess);
    EXPECT_EQ(result.status, kFakeError);
}

/**
 * @brief An empty hook table still opens and commits a transaction.
 */
TEST(UnitHookTransaction, EmptyTable)
{
    ResetFixture();

    const appbox::HookTransactionResult result =
        appbox::ApplyHookTransaction(kHooks, 0, appbox::HookAction::Attach, MakeFakeOps());

    EXPECT_TRUE(result.bSuccess);

    const std::vector<std::string> expected = { "begin", "update_thread", "commit" };
    EXPECT_EQ(g_ops, expected);
}

/**
 * @brief The operations backed by the Detours library are available.
 */
TEST(UnitHookTransaction, DefaultOpsAreAvailable)
{
    const appbox::DetourOps& ops = appbox::DefaultDetourOps();

    EXPECT_TRUE(ops.fn_begin != nullptr);
    EXPECT_TRUE(ops.fn_update_thread != nullptr);
    EXPECT_TRUE(ops.fn_attach != nullptr);
    EXPECT_TRUE(ops.fn_detach != nullptr);
    EXPECT_TRUE(ops.fn_commit != nullptr);
}
