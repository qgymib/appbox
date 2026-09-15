#ifndef APPBOX_SANDBOX_HOOK_TRANSACTION_HPP
#define APPBOX_SANDBOX_HOOK_TRANSACTION_HPP

#include "__init__.hpp"
#include <cstddef>

namespace appbox
{

/**
 * @brief Action applied to every hook of the table.
 */
enum class HookAction
{
    Attach,
    Detach,
};

/**
 * @brief Result of a hook transaction.
 */
struct HookTransactionResult
{
    bool        bSuccess = false;    /* Whether the transaction was committed. */
    const char* pFailedHook = nullptr; /* Name of the failing hook, null when none. */
    LONG        status = 0;          /* Status of the failing Detour operation. */
};

/**
 * @brief Detour operations used by ApplyHookTransaction().
 *
 * The operations are injected so the transaction logic can be verified without
 * touching real function addresses.
 */
struct DetourOps
{
    LONG (*fn_begin)();                  /* Begin a transaction. */
    LONG (*fn_update_thread)(HANDLE);    /* Add a thread to the transaction. */
    LONG (*fn_attach)(void**, void*);    /* Attach one detour. */
    LONG (*fn_detach)(void**, void*);    /* Detach one detour. */
    LONG (*fn_commit)();                 /* Commit the transaction. */
};

/**
 * @brief Get the Detour operations backed by the Detours library.
 * @return The default operations.
 */
const DetourOps& DefaultDetourOps();

/**
 * @brief Attach or detach every hook with a detour inside a single transaction.
 *
 * Hooks without a detour are skipped, they only carry a resolved function
 * address. The transaction is always closed, even when one of the operations
 * fails, so no transaction stays open. Hooks that were already processed keep
 * their state when a later hook fails; the caller decides whether this is
 * fatal.
 *
 * @param[in] hooks Hook table, an array of hook record pointers.
 * @param[in] count Number of hooks in the table.
 * @param[in] action Attach or detach.
 * @param[in] ops Detour operations.
 * @return The transaction result.
 */
HookTransactionResult ApplyHookTransaction(const HookRecord* const* hooks, size_t count, HookAction action,
                                           const DetourOps& ops);

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_TRANSACTION_HPP
