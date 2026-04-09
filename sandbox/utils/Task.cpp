#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <atomic>
#include <list>
#include <mutex>
#include "Task.hpp"

struct appbox::TaskQueue::Data
{
    Data();

    std::atomic_bool                   looping;
    HANDLE                             thread;
    std::list<appbox::TaskQueue::Task> task_queue;
    std::mutex                         task_queue_mutex_;
    HANDLE                             task_queue_sem_;
};

static DWORD CALLBACK TaskQueueThread(LPVOID lpParameter)
{
    auto ctx = static_cast<appbox::TaskQueue::Data*>(lpParameter);
    while (ctx->looping)
    {
        WaitForSingleObject(ctx->task_queue_sem_, 100);

        appbox::TaskQueue::Task fn;
        {
            std::lock_guard<std::mutex> lock(ctx->task_queue_mutex_);
            if (ctx->task_queue.empty())
            {
                continue;
            }
            fn = std::move(ctx->task_queue.front());
            ctx->task_queue.pop_front();
        }
        fn();
    }

    return 0;
}

appbox::TaskQueue::Data::Data() : looping(true)
{
    task_queue_sem_ = CreateSemaphoreW(nullptr, 0, 1000, nullptr);
    thread = CreateThread(nullptr, 0, TaskQueueThread, this, 0, nullptr);
}

appbox::TaskQueue::TaskQueue()
{
    data_ = std::make_shared<Data>();
}

appbox::TaskQueue::Ptr appbox::TaskQueue::Create()
{
    Ptr obj(new TaskQueue);
    return obj;
}

void appbox::TaskQueue::Submit(Task task)
{
    {
        std::lock_guard<std::mutex> lock(data_->task_queue_mutex_);
        data_->task_queue.push_back(std::move(task));
    }

    ReleaseSemaphore(data_->task_queue_sem_, 1, nullptr);
}