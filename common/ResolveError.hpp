#ifndef APPBOX_COMMON_RESOLVE_ERROR_HPP
#define APPBOX_COMMON_RESOLVE_ERROR_HPP

/*
 * The failure of the shared resolver.
 *
 * The resolver sits between the rules of the view and the file system of a
 * layer, so a failure it reports has two parts: the class of the failure, which
 * is a code of `common/ErrorCode.hpp` like every other failure of `common/`,
 * and the status the file system reported, which the caller forwards to the
 * process it serves. The two parts travel together, because a caller which
 * returns the class alone loses the status of the layer, and a caller which
 * returns the status alone cannot branch on the reason.
 *
 * The header is small on purpose: every header which resolves a path of the
 * view includes it, and the sandbox compiles those headers into the DLL it
 * injects, so nothing but the two fields is defined here.
 */

#include "WinAPI.h" /* Must be first include file */
#include "ErrorCode.hpp"
#include <cstdint>

namespace appbox::filesystem
{

/**
 * @brief Failure of a call of the shared resolver.
 *
 * A call which failed produces one of these instead of its answer. The status
 * is the one the file system of the layer reported, unchanged, so a caller
 * which forwards it behaves exactly like it did when the resolver answered an
 * `NTSTATUS`: the status of a link which the view could not resolve, of a read
 * of the data of a reparse point and of a lookup of a component all reach the
 * process the sandbox serves.
 *
 * `STATUS_SUCCESS` means the failure was decided by the view alone and the file
 * system reported nothing about it - a target of another namespace, a chain of
 * links which is too deep, a path which carries no drive of the view. A caller
 * which needs a status for such a failure picks one which keeps its own
 * contract, and the resolver names the class of the failure in `code`.
 */
struct ResolveError
{
    /** Class of the failure, see `common/ErrorCode.hpp`. */
    ErrorCode code = ErrorCode::kInvalidArgument;

    /**
     * @brief Status the file system reported, `STATUS_SUCCESS` when the view
     *        decided the failure alone.
     */
    NTSTATUS status = STATUS_SUCCESS;
};

/**
 * @brief Turn a failure of the resolver into the status a caller forwards.
 *
 * A caller which serves an `NTSTATUS` contract of its own - the hooks of the
 * sandbox - reports the status of the failure. A failure the view decided alone
 * carries no status of a layer, so the call answers the status the resolver
 * used before the class of the failure existed, which keeps the contract of
 * such a caller unchanged.
 *
 * @param[in] error Failure of the resolver.
 * @return The status of the file system, or `STATUS_REPARSE_POINT_NOT_RESOLVED`
 *         when the view alone decided the failure.
 */
constexpr NTSTATUS ResolveStatusOf(const ResolveError& error) noexcept
{
    return error.status != STATUS_SUCCESS ? error.status : STATUS_REPARSE_POINT_NOT_RESOLVED;
}

} // namespace appbox::filesystem

#endif // APPBOX_COMMON_RESOLVE_ERROR_HPP
