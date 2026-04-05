#include <spdlog/spdlog.h>
#include "PipeSession.hpp"
#include "PipeClient.hpp"

#include "spdlog/fmt/bundled/os.h"

struct appbox::PipeClient::Data : std::enable_shared_from_this<Data>
{
    Data();
    ~Data();

    void ConnectThread();

    PipeSession::Ptr session;    /* RPC session. */
    asio::io_context io_context; /* IO context */
    std::wstring     pipe_path;  /* Named pipe path. */
    std::thread*     thread;     /* Work thread */
};

appbox::PipeClient::Data::Data() : thread(nullptr)
{
}

appbox::PipeClient::Data::~Data()
{
    if (thread != nullptr)
    {
        thread->join();
        delete thread;
        thread = nullptr;
    }
}

void appbox::PipeClient::Data::ConnectThread()
{
    HANDLE pipe = nullptr;
    for (size_t i = 0; i < 5; ++i)
    {
        pipe = CreateFileW(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
        {
            break;
        }
        if (GetLastError() != ERROR_PIPE_BUSY)
        {
            SPDLOG_ERROR(L"cannot open pipe {}", pipe_path);
            return;
        }

        if (!WaitNamedPipeW(pipe_path.c_str(), 1 * 1000))
        {
            SPDLOG_ERROR("cannot open pipe");
            return;
        }
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    // TODO
}

appbox::PipeClient::PipeClient(const std::wstring& pipe_path)
{
    data_ = std::make_shared<Data>();
    data_->pipe_path = pipe_path;
}

appbox::PipeClient::~PipeClient()
{
    data_.reset();
}

bool appbox::PipeClient::Start()
{
    if (data_->thread != nullptr)
    {
        SPDLOG_ERROR("PipeClient already started");
        return false;
    }

    data_->thread = new std::thread(&Data::ConnectThread, data_);
    data_->thread->join();
    delete data_->thread;
    data_->thread = nullptr;

    return true;
}
