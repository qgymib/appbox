#ifndef APPBOX_LOADER_UTILS_PATCH_LAYER_HPP
#define APPBOX_LOADER_UTILS_PATCH_LAYER_HPP

#include <string>
#include <vector>
#include "utils/SandboxPaths.hpp"

namespace appbox
{

/**
 * @brief One patch package which takes part in a run.
 *
 * The package was validated and extracted into its cache entry, so the
 * resources it carries can be handed to the sandbox. A package which carries
 * no filesystem resource keeps the layer root empty and a package which sets
 * no filesystem isolation mode keeps the isolation file empty, because a
 * patch overrides the resources it carries and not the resources of the
 * layers below it.
 */
struct PatchLayer
{
    /**
     * @brief Name of the cache entry of the package.
     *
     * The entry is the package file without its extension, see
     * SandboxPaths::PatchCacheName().
     */
    std::wstring name;

    /**
     * @brief Root of the filesystem layers of the package.
     *
     * Empty when the package carries no `filesystem` directory.
     */
    std::wstring layer_root;

    /**
     * @brief Isolation modes of the filesystem workspace of the package.
     *
     * Empty when the package carries no isolation file.
     */
    std::wstring isolation_file;

    /**
     * @brief Virtual registry of the package.
     *
     * Empty when the package carries no `registry/user.hiv`. The loader merges
     * the hive into the hive the sandbox mounts, so the keys and the values of
     * the package override the entries of the same name of the layers below it
     * while the entries it does not name stay in place.
     */
    std::wstring registry_hive;

    /**
     * @brief Isolation modes of the registry workspace of the package.
     *
     * Empty when the package carries no `registry/isolation.json`.
     */
    std::wstring registry_isolation_file;

    /**
     * @brief Network configuration of the package.
     *
     * Empty when the package carries no `network/isolation.json`. The sandbox
     * merges the DNS redirections of the file into the redirections of the
     * layers below it and lets the last file which names a usable proxy pick
     * the proxy of the run, so a package which carries no file keeps the
     * network configuration below it.
     */
    std::wstring network_isolation_file;

    /**
     * @brief Environment variables of the package.
     *
     * Empty when the package carries no `environment/isolation.json`. The
     * sandbox composes the environment of the run layer by layer, so a
     * package which carries no file keeps the environment below it.
     */
    std::wstring environment_isolation_file;
};

/**
 * @brief Load the patch packages of a run.
 *
 * The packages of the `patch` directory next to the loader configuration are
 * read in ascending name order, which is the order they take effect in: a
 * later package overrides an earlier one and every package overrides the
 * resources of `app`. A package which cannot be read is logged and skipped
 * like a malformed isolation file is, so a broken package never fails the
 * run.
 *
 * The digest of a package decides whether its cache entry is still valid: a
 * package whose digest is recorded in its cache entry is reused as it is,
 * every other package is extracted into the cache again. The cache directory
 * is created as soon as the patch directory holds at least one package.
 *
 * A missing patch directory is not an error: a run without patches is a run
 * without layers above the resources of the archive.
 *
 * @param[in] paths The resolved layout of the run.
 * @return The accepted packages in ascending name order, which is the order
 *         the layers of the run are applied in.
 */
std::vector<PatchLayer> LoadPatchLayers(const SandboxPaths& paths);

} // namespace appbox

#endif // APPBOX_LOADER_UTILS_PATCH_LAYER_HPP
