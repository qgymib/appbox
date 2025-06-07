#ifndef APPBOX_UTILS_INSTANCE_HPP
#define APPBOX_UTILS_INSTANCE_HPP

#include "utils/winapi.hpp"
#include <memory>

namespace appbox
{

/**
 * @brief A class responsible for managing instances of a specific resource or object.
 *
 * This class is designed to provide centralized control and access to instances
 * of a particular type or resource. It ensures that instance creation, management,
 * and destruction follow the desired lifecycle and resource management protocols.
 *
 * The class implementation may include mechanisms such as lazy-loading, caching,
 * or reference counting to optimize performance and memory usage. It acts as a
 * utility for managing instances in a consistent and controlled manner.
 */
template <typename T>
class Instance
{
public:
    Instance() : mOnceToken(INIT_ONCE_STATIC_INIT)
    {
    }

    /**
     * @brief Provides a thread-safe mechanism to initialize and retrieve a singleton instance of
     * type T.
     *
     * The instance is lazily created upon the first access to this method and ensures it is
     * only constructed once through the use of one-time initialization primitives.
     *
     * @return A pointer to the singleton instance of type T.
     */
    T* Get()
    {
        BOOL fPending = false;
        InitOnceBeginInitialize(&mOnceToken, 0, &fPending, nullptr);
        if (fPending)
        {
            mInstance = std::make_shared<T>();
            InitOnceComplete(&mOnceToken, 0, nullptr);
        }

        return mInstance.get();
    }

    /**
     * @brief Overloads the operator to provide a specific functionality or customized behavior.
     *
     * This method allows objects to interact using the defined operator, offering intuitive
     * and concise interactions, typically adhering to standard operator semantics.
     *
     * @return The result of applying the operator, typically matching the expected type or
     * behavior.
     */
    T* operator->()
    {
        return Get();
    }

private:
    INIT_ONCE          mOnceToken;
    std::shared_ptr<T> mInstance;
};

} // namespace appbox

#endif
