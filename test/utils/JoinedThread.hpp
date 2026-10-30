#ifndef APPBOX_TEST_UTILS_JOINEDTHREAD_HPP
#define APPBOX_TEST_UTILS_JOINEDTHREAD_HPP

#include <exception>
#include <functional>
#include <memory>
#include <thread>

namespace appbox::test
{

/**
 * @brief A thread of a case which is joined when its owner is released.
 *
 * A case starts a launcher run on a thread of its own, because the run returns
 * only when its last program left, and works with the run meanwhile. A case
 * which fails while the run works would release a plain `std::thread` which is
 * still joinable, and the destructor of such a thread ends the whole test
 * executable: the failure of the case reaches no reader, every case after it is
 * lost, and the programs of the run keep working without the process which
 * asked for them.
 *
 * The object joins instead, so the failure of the case reaches the case which
 * reports it. A failure of the thread itself is kept as well: a run which
 * throws inside its thread would otherwise end the test executable, so the
 * failure is reported by the call which joins the thread.
 */
class JoinedThread
{
public:
    /**
     * @brief Start the thread of the case.
     * @param[in] body Work of the thread, which may throw.
     */
    explicit JoinedThread(std::function<void()> body);

    /**
     * @brief Join the thread, so a case which fails does not end the executable.
     *
     * A failure of the body which the case did not take is reported in the log
     * of the run: the case which owns the thread is already reporting the
     * failure it ended with, so the thread is not the reason the case failed.
     */
    ~JoinedThread();

    JoinedThread(const JoinedThread&) = delete;
    JoinedThread& operator=(const JoinedThread&) = delete;

    /**
     * @brief Take over the thread of another object.
     *
     * The move exists for the containers of the threads of a case: a container
     * which grows moves its elements while the threads are still working, and
     * the failure of a thread is kept outside of the object for that reason.
     *
     * @param[in,out] other The object to take over.
     */
    JoinedThread(JoinedThread&& other) noexcept;

    JoinedThread& operator=(JoinedThread&& other) = delete;

    /**
     * @brief Join the thread and report the failure of its body.
     *
     * The call may be repeated: a thread which already ended is not joined a
     * second time, and its failure is reported again to every caller.
     *
     * @throws The failure the body of the thread ended with.
     */
    void Join();

private:
    /**
     * @brief The failure of the body, which the thread writes and the owner reads.
     *
     * The failure is shared instead of being a member of this object, so a
     * thread which is moved to another object still reports into the state the
     * owner reads after it joined.
     */
    struct State
    {
        std::exception_ptr error; /* Failure of the body, empty when it ended well. */
    };

    std::shared_ptr<State> state_;  /* Failure of the body */
    std::thread            thread_; /* Thread of the case */
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_JOINEDTHREAD_HPP
