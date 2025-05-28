#ifndef APPBOX_UTILS_LAYOUT_HPP
#define APPBOX_UTILS_LAYOUT_HPP

#include <cstdint>

namespace appbox
{

struct LayoutSection
{
    uint64_t offset; /* Start position of the section. */
    uint64_t length; /* Section length. */
};

struct Layout
{
    LayoutSection meta;       /* Metadata */
    LayoutSection filesystem; /* Filesystem files. */
    LayoutSection sandbox;    /* Sandbox dll. */
};

} // namespace appbox

#endif
