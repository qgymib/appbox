#ifndef APPBOX_SANDBOX_FILESYSTEM_RESOLVE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_RESOLVE_HPP

#include "ResolveTypes.hpp"
#include <string>

namespace appbox::filesystem
{

/*
 * The answer of the resolver and the layers it resolves in are the vocabulary
 * the sandbox shares with the launcher, so they live in `common/ResolveTypes.hpp`
 * next to the resolver itself (`common/ResolveCore.hpp`). This header adds the
 * two entry points of the sandbox, which resolve a path with the operations of
 * this process and with the layers of the sandbox it runs in.
 */

/**
 * @brief Resolve virtual path to host path.
 * @param[in] vPath Virtual path in mapped view.
 * @param[in] option Resolve option.
 * @return Resolve result.
 */
ResolveResult::Ptr Resolve(const std::wstring& vPath, const ResolveOption& option = {});

/**
 * @brief Resolve virtual path to host path.
 *
 * The path is mapped to the layers by text, and the reparse points of the path
 * are resolved by the view itself first: a junction, a symbolic link or a
 * mount point is followed by reading its target out of the layer which holds
 * it, so the isolation of the target decides the answer instead of the
 * isolation of the path the caller spelled. A reparse point which redirects
 * the namespace and which cannot be turned into a path of the view fails the
 * resolution, see `ResolveResult::reparseStatus`.
 *
 * @param[in] fs Resolve file system.
 * @param[in] vPath Virtual path in mapped view.
 * @param[in] option Resolve option.
 * @param[in] isolation Isolation modes of the virtual filesystem, null when
 *                      the view is resolved without an isolation.
 * @return Resolve result.
 */
ResolveResult::Ptr ResolveFull(const ResolveFs& fs, const std::wstring& vPath, const ResolveOption& option,
                               const IsolationTable* isolation = nullptr);

} // namespace appbox::filesystem

#endif
