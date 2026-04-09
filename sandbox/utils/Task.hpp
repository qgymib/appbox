#ifndef APPBOX_SANDBOX_UTILS_TASK_HPP
#define APPBOX_SANDBOX_UTILS_TASK_HPP

#include <memory>
#include <functional>

namespace appbox
{

class TaskQueue
{
public:
    typedef std::shared_ptr<TaskQueue> Ptr;
    typedef std::function<void()>      Task;

    static Ptr Create();
    void       Submit(Task task);

    struct Data;
    std::shared_ptr<Data> data_;

private:
    TaskQueue();
};

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_TASK_HPP
