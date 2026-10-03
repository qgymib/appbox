#include "tracer/Category.hpp"

namespace appbox::tracer
{

std::vector<Category> AllCategories()
{
    return { Category::File, Category::Registry, Category::Network };
}

} // namespace appbox::tracer
