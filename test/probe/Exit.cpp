#include <atomic>
#include "Exit.hpp"

namespace
{

/** Whether a request asked this process to leave, written by the IO thread. */
std::atomic_bool s_exit_requested = false;

/** Exit code the request named, written by the IO thread. */
std::atomic<std::int32_t> s_exit_code = 0;

} // namespace

appbox::test::ProtocolExit::Rsp appbox::test::ProbeExit_Entry(const appbox::test::ProtocolExit::Req& request)
{
    /*
     * The process leaves once the answer of this request was written, which is
     * what the serve loop of `test/probe/__init__.cpp` waits for: a process
     * which left here would drop the answer and the caller of the request would
     * wait for its whole budget.
     */
    s_exit_code = request.code;
    s_exit_requested = true;

    return appbox::test::ProtocolExit::Rsp{};
}

bool appbox::test::ExitRequested()
{
    return s_exit_requested.load();
}

std::int32_t appbox::test::ExitCode()
{
    return s_exit_code.load();
}
