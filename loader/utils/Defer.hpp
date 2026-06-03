#ifndef APPBOX_LOADER_UTILS_DEFER_HPP
#define APPBOX_LOADER_UTILS_DEFER_HPP

#include <functional>

namespace appbox
{

struct Defer
{
    Defer(std::function<void()> func) : func_(func)
    {
    }

    ~Defer()
    {
        func_();
    }

    std::function<void()> func_;
};

} // namespace appbox

#endif
