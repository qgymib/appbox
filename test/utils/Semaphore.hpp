#ifndef APPBOX_TEST_UTILS_SEMAPHORE_HPP
#define APPBOX_TEST_UTILS_SEMAPHORE_HPP

#include <cstdint>
#include <cstddef>

namespace appbox
{

struct Semaphore
{
    Semaphore();
    virtual ~Semaphore();

    /**
     * @brief Release the semaphore.
     * @param[in] count Number of times to release the semaphore.
     */
    void Release(size_t count = 1);

    /**
     * @brief Acquire the semaphore.
     * @param[in] timeout_ms Timeout in milliseconds.
     * @return True if the semaphore was acquired, false if the timeout was reached.
     */
    bool Acquire(uint32_t timeout_ms = (uint32_t)-1);

    Semaphore(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore& operator=(Semaphore&&) = delete;

    struct Data;
    Data* data_;
};

} // namespace appbox

#endif
