#include <spdlog/spdlog.h>
#include <exception>
#include <utility>
#include "JoinedThread.hpp"

appbox::test::JoinedThread::JoinedThread(std::function<void()> body) : state_(std::make_shared<State>())
{
    /*
     * The thread keeps the state and not this object, so a thread which is
     * moved to another object keeps reporting into the state its owner reads.
     */
    auto state = state_;
    thread_ = std::thread([state, body = std::move(body)]() {
        try
        {
            body();
        }
        catch (...)
        {
            state->error = std::current_exception();
        }
    });
}

appbox::test::JoinedThread::JoinedThread(JoinedThread&& other) noexcept
    : state_(std::move(other.state_)), thread_(std::move(other.thread_))
{
}

appbox::test::JoinedThread::~JoinedThread()
{
    if (!thread_.joinable())
    {
        return;
    }

    thread_.join();

    if (!state_ || !state_->error)
    {
        return;
    }

    /*
     * The case which owns the thread is reporting the failure it ended with, so
     * the failure of the thread is reported in the log of the run instead of
     * ending the process.
     */
    try
    {
        std::rethrow_exception(state_->error);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("a thread of the case failed and the case did not take the failure: {}", e.what());
    }
    catch (...)
    {
        SPDLOG_ERROR("a thread of the case failed and the case did not take the failure");
    }
}

void appbox::test::JoinedThread::Join()
{
    if (thread_.joinable())
    {
        thread_.join();
    }

    if (state_ && state_->error)
    {
        std::rethrow_exception(state_->error);
    }
}
