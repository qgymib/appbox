#ifndef APPBOX_SANDBOX_FILESYSTEM_QUERY_PATH_HPP
#define APPBOX_SANDBOX_FILESYSTEM_QUERY_PATH_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "Resolve.hpp"
#include <string>

namespace appbox::filesystem
{

/**
 * @brief Outcome of the path of a query which carries a name.
 *
 * The NT file entry points which take a name instead of a handle all have to
 * look the name up in the view before the call is forwarded. The three states
 * below are the whole contract of that lookup: the caller either forwards its
 * call unchanged, reports the failure of the view or forwards it against the
 * layer path the view selected.
 */
struct QueryPathResult
{
    enum class Outcome
    {
        /**
         * @brief The name is not a path of the view.
         *
         * The name cannot be expressed as a view path (a UNC path, a named
         * pipe, a mailslot, a network volume, a drive relative path) or the
         * attributes of the caller cannot be read. The call belongs to another
         * isolation domain, so the caller forwards it unchanged.
         */
        Forward,

        /**
         * @brief The name is a path of the view which no visible layer holds.
         */
        NotFound,

        /**
         * @brief The name is a path of the view which `layerPath` holds.
         */
        Found,
    };

    /**
     * @brief Result of the lookup.
     */
    Outcome outcome = Outcome::Forward;

    /**
     * @brief Failure status of the view.
     *
     * Set when the outcome is `NotFound`: `STATUS_OBJECT_PATH_NOT_FOUND` when
     * the parent directory of the name is not part of the view and
     * `STATUS_OBJECT_NAME_NOT_FOUND` when the entry is missing, hidden by a
     * whiteout or an opaque marker or hidden by the isolation.
     */
    NTSTATUS status = STATUS_SUCCESS;

    /**
     * @brief NT path of the first visible layer which holds the name.
     *
     * Set when the outcome is `Found`. The upper layer is preferred, then the
     * lower layers and the host layer last, which is the order the resolver
     * reports its hits in; a layer which the isolation of the path hides is not
     * part of the result.
     */
    std::wstring layerPath;
};

/**
 * @brief Report how a resolved path of the view has to be answered.
 *
 * The helper turns the result of the resolver into the vocabulary of a call
 * which carries a name: a path whose parent directory is not part of the view
 * and a path which no visible layer holds are failures of the view, and a path
 * which a visible layer holds is answered from the layer the view prefers.
 *
 * @param[in] resolve Result of the resolver.
 * @return The outcome of the lookup, the failure status of the view and the
 *         layer path which holds the name.
 */
QueryPathResult QueryPathFromResolve(const ResolveResult& resolve);

/**
 * @brief Resolve a path of the view and report how a caller has to answer it.
 *
 * The helper is the front end of every hooked NT entry point which carries a
 * name of its own: it resolves the name in the view and reports which layer
 * the caller has to address.
 *
 * @param[in] viewPath Path of the view.
 * @param[in] nameAttributes Lookup attributes of the call, see
 *                           `ResolveOption::NameAttributes`.
 * @param[in] stopOnFirstFound Whether the search may stop at the first layer
 *                             which holds the path. A call which may modify
 *                             the entry asks for every layer, because the
 *                             isolation of a `Merge` path decides the layer of
 *                             the modification from them.
 * @return The outcome of the lookup, the failure status of the view and the
 *         layer path which holds the name.
 */
QueryPathResult ResolveViewPath(const std::wstring& viewPath, ULONG nameAttributes, bool stopOnFirstFound = true);

/**
 * @brief Resolve the name of a query to the first visible layer of the view.
 *
 * The helper is the common front end of every hooked NT entry point which
 * takes a name instead of a handle: it converts the attributes of the caller
 * into a view path, resolves that path in the view and reports which layer the
 * caller has to query. A name which cannot be expressed as a view path is
 * reported as `Forward`, so the caller keeps the behaviour of the operating
 * system for the paths of the other isolation domains.
 *
 * @param[in] ObjectAttributes Attributes of the call, may be null.
 * @return The outcome of the lookup, the failure status of the view and the
 *         layer path which holds the name.
 */
QueryPathResult ResolveQueryPath(const POBJECT_ATTRIBUTES ObjectAttributes);

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_QUERY_PATH_HPP
