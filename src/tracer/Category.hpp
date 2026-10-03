#ifndef APPBOX_TRACER_CATEGORY_HPP
#define APPBOX_TRACER_CATEGORY_HPP

#include <vector>

namespace appbox::tracer
{

/**
 * @brief Categories of traced functions.
 *
 * The categories are the isolation domains of appbox (filesystem, registry and network), and
 * the scope of a category is the set of lowest level entry points of that domain. The scope
 * does not depend on the hooks the sandbox implements: the sandbox may cover a part of the
 * set, and the tracer still reports the whole domain, because it is a debugging tool.
 */
enum class Category
{
    File,     ///< Filesystem related exports.
    Registry, ///< Registry related exports.
    Network,  ///< Network related exports (named pipes, mailslots, device control).
};

/** @return Every category, in the order they are documented. */
std::vector<Category> AllCategories();

} // namespace appbox::tracer

#endif // APPBOX_TRACER_CATEGORY_HPP
