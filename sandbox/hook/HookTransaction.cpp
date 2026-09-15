#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include "HookTransaction.hpp"

const appbox::DetourOps& appbox::DefaultDetourOps()
{
    static const DetourOps s_ops = {
        []() -> LONG { return DetourTransactionBegin(); },
        [](HANDLE thread) -> LONG { return DetourUpdateThread(thread); },
        [](void** ppPointer, void* pDetour) -> LONG { return DetourAttach(ppPointer, pDetour); },
        [](void** ppPointer, void* pDetour) -> LONG { return DetourDetach(ppPointer, pDetour); },
        []() -> LONG { return DetourTransactionCommit(); },
    };

    return s_ops;
}

appbox::HookTransactionResult appbox::ApplyHookTransaction(const HookRecord* const* hooks, size_t count,
                                                          HookAction action, const DetourOps& ops)
{
    HookTransactionResult result;

    LONG status = ops.fn_begin();
    if (status != NO_ERROR)
    {
        result.status = status;
        return result;
    }

    status = ops.fn_update_thread(GetCurrentThread());
    if (status != NO_ERROR)
    {
        result.status = status;
        /* Close the transaction which was just opened. */
        ops.fn_commit();
        return result;
    }

    for (size_t i = 0; i < count; ++i)
    {
        const HookRecord* hook = hooks[i];
        if (hook == nullptr || hook->pDetour == nullptr)
        {
            /* The hook only carries a resolved function address. */
            continue;
        }

        status = (action == HookAction::Attach) ? ops.fn_attach(hook->ppPointer, hook->pDetour)
                                               : ops.fn_detach(hook->ppPointer, hook->pDetour);
        if (status != NO_ERROR)
        {
            result.pFailedHook = hook->name;
            result.status = status;
            /* Close the transaction, the hooks processed so far keep their state. */
            ops.fn_commit();
            return result;
        }
    }

    status = ops.fn_commit();
    if (status != NO_ERROR)
    {
        result.status = status;
        return result;
    }

    result.bSuccess = true;
    return result;
}
