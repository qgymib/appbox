#ifndef APPBOX_SANDBOX_UTILS_ASYNC_INSTANCE_HPP
#define APPBOX_SANDBOX_UTILS_ASYNC_INSTANCE_HPP

#include <memory>
#include <functional>
#include <mutex>
#include <thread>

namespace appbox
{

template <typename T>
class AsyncInstance : public std::enable_shared_from_this<AsyncInstance<T>>
{
public:
    typedef std::shared_ptr<AsyncInstance<T>>   Ptr;
    typedef std::weak_ptr<AsyncInstance<T>>     WeakPtr;
    typedef std::function<std::shared_ptr<T>()> InstanceFactory;

    static Ptr Create(InstanceFactory factory)
    {
        Ptr obj(new AsyncInstance<T>);
        obj->factory_ = factory;

        return obj;
    }

    T* operator->()
    {
        std::call_once(once_, [this]() { this->AsyncCreate(); });
        return obj_.get();
    }

private:
    void AsyncCreate()
    {
        std::thread t([this]() { this->obj_ = this->factory_(); });
        t.join();
    }

    InstanceFactory    factory_;
    std::once_flag     once_;
    std::shared_ptr<T> obj_;
};

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_ASYNC_INSTANCE_HPP
