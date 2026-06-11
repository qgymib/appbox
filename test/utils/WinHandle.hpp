#ifndef APPBOX_TEST_UTILS_WINHANDLE_HPP
#define APPBOX_TEST_UTILS_WINHANDLE_HPP

#include <functional>

namespace appbox::test
{

/**
 * @brief RAII wrapper for Windows handles.
 * @tparam T Windows handle type.
 */
template <typename T>
class WinHandle
{
public:
    typedef std::function<void(T)> CloseFn;

    /**
     * @brief Construct a new empty Win Handle object.
     * @param[in] fn Close function.
     */
    WinHandle(CloseFn fn) : f_close_(fn), h_(nullptr)
    {
    }

    ~WinHandle()
    {
        if (h_ != nullptr)
        {
            f_close_(h_);
            h_ = nullptr;
        }
    }

    /**
     * @brief Get the handle.
     * @return T& Handle.
     */
    T& Ref()
    {
        return h_;
    }

    /**
     * @brief Get address of the handle.
     * @return Handle address.
     */
    T* operator&()
    {
        return &h_;
    }

private:
    CloseFn f_close_;
    T       h_;
};

} // namespace appbox::test

#endif
