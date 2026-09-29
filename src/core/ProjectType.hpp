#ifndef APPBOX_PACKER_CORE_PROJECT_TYPE_HPP
#define APPBOX_PACKER_CORE_PROJECT_TYPE_HPP

#include <cstddef>
#include <string_view>

namespace appbox
{

/**
 * @brief Kind of product a pack run writes.
 *
 * The type is chosen by the `Project Type` box of the ribbon and travels with
 * the project file, so it is a property of the session and not of the models
 * of the workspaces.
 *
 * - `Standalone` - the self-contained archive of `Pack()`: the loader program
 *   named after the first startup file, its configuration and the read-only
 *   resources below `app`. The archive is extracted and started on its own.
 * - `Patch` - the patch package of `PackPatch()`: the very same resources,
 *   rooted at the archive root instead of below `app`, and without the loader
 *   program and its configuration. The package is dropped into the `patch`
 *   directory next to the loader of a standalone archive, which merges every
 *   patch of that directory in ascending name order.
 */
enum class ProjectType
{
    Standalone, ///< Self-contained archive with the loader and the resources.
    Patch       ///< Patch package with the resources of `app`, without a loader.
};

/**
 * @brief Get the token of a project type.
 *
 * The token is the text form of the type, which the project file stores and
 * reads.
 *
 * @param[in] type The project type.
 * @return The canonical lower case token, either `"standalone"` or `"patch"`.
 */
const char* ProjectTypeToken(ProjectType type);

/**
 * @brief Resolve a project type from its token.
 *
 * The comparison ignores the case, so the tokens of the project file are read
 * whatever the case they were written with.
 *
 * @param[in] token The token to resolve.
 * @param[out] type The resolved type when the token is known.
 * @return true when the token names a project type.
 */
bool ParseProjectTypeToken(std::string_view token, ProjectType& type);

/**
 * @brief Get the label of a project type as the `Project Type` box shows it.
 *
 * The label is the text of the entry of the box, so the box and the project
 * file never spell a type differently.
 *
 * @param[in] type The project type.
 * @return The label, either `L"Standalone (ZIP)"` or `L"Patch (ZIP)"`.
 */
const wchar_t* ProjectTypeDisplayName(ProjectType type);

/**
 * @brief Get the number of entries the `Project Type` box offers.
 * @return The number of project types.
 */
std::size_t ProjectTypeCount();

/**
 * @brief Get the project type of one entry of the `Project Type` box.
 *
 * The box stores the entries in the order of ProjectTypeIndexOf(), so the
 * selection of the box is the index of the type it shows.
 *
 * @param[in] index Index of the entry.
 * @return The type of the entry; `Standalone` when the index is outside the
 *         box, so an unknown selection never turns into an unexpected product.
 */
ProjectType ProjectTypeAt(std::size_t index);

/**
 * @brief Get the index of a project type inside the `Project Type` box.
 * @param[in] type The project type.
 * @return The index of the entry; the index of `Standalone` when the type is
 *         not part of the box.
 */
std::size_t ProjectTypeIndexOf(ProjectType type);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PROJECT_TYPE_HPP
