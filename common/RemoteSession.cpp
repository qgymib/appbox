#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <windows.h>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <list>
#include <array>
#include "RpcCodec.hpp"
#include "RemoteSession.hpp"

struct appbox::RemoteSession::Data : std::enable_shared_from_this<Data>
{
    Data();

    void WantWrite();
    void WantRead();
    void OnRead();

    std::shared_ptr<asio::windows::stream_handle> pipe;       /* Named pipe */
    DataReceivedCallback                          cb;         /* Recv callback */
    std::list<MsgPtr>                             send_queue; /* Send queue */
    std::vector<uint8_t>                          recv_data;  /* Recv data */
    std::array<uint8_t, 4096>                     recv_buff;  /* Recv cache */
    bool                                          in_write;   /* During write. */
};

appbox::RemoteSession::Data::Data() : in_write(false)
{
}

void appbox::RemoteSession::Data::WantWrite()
{
    if (in_write)
    {
        return;
    }
    if (send_queue.empty())
    {
        return;
    }

    auto self = shared_from_this();
    auto msg = send_queue.front();

    self->in_write = true;
    asio::async_write(*pipe, asio::buffer(*msg), [self](const asio::error_code& ec, std::size_t) {
        self->in_write = false;
        self->send_queue.pop_front();
        if (ec)
        { /* write failed */
            SPDLOG_ERROR("[HANDLE: {}] write pipe failed: {}", self->pipe->native_handle(), ec.message());
            return;
        }

        self->WantWrite();
    });
}

void appbox::RemoteSession::Data::WantRead()
{
    auto self = shared_from_this();
    auto fn = [self](const asio::error_code& ec, std::size_t n) {
        if (ec)
        {
            SPDLOG_DEBUG("[HANDLE: {}] read pipe failed: {}", self->pipe->native_handle(), ec.message());
            self->cb(ec, MsgPtr());
            return;
        }

        size_t old_sz = self->recv_data.size();
        size_t new_sz = old_sz + n;
        self->recv_data.resize(new_sz);
        memcpy(self->recv_data.data() + old_sz, self->recv_buff.data(), n);

        self->OnRead();
        self->WantRead();
    };

    asio::async_read(*pipe, asio::buffer(recv_buff), asio::transfer_at_least(1), fn);
}

void appbox::RemoteSession::Data::OnRead()
{
    /*
     * One read may deliver several frames at once or a part of the next frame,
     * so the buffer is drained until it does not hold a complete frame any
     * more.
     */
    for (;;)
    {
        std::string payload;
        std::string error;

        const FrameStatus status = TryDecodeFrame(recv_data, payload, error);
        if (status == FrameStatus::NeedMoreData)
        {
            return;
        }

        if (status == FrameStatus::ProtocolError)
        {
            SPDLOG_ERROR("[HANDLE: {}] {}", pipe->native_handle(), error);
            /* Report the failure instead of throwing out of the asio callback. */
            cb(asio::error_code(asio::error::invalid_argument), MsgPtr());
            return;
        }

        cb(asio::error_code(), std::make_shared<std::string>(std::move(payload)));
    }
}

appbox::RemoteSession::RemoteSession()
{
    data_ = std::make_shared<Data>();
}

appbox::RemoteSession::~RemoteSession()
{
    data_.reset();
}

appbox::RemoteSession::Ptr appbox::RemoteSession::Create(std::shared_ptr<asio::windows::stream_handle> pipe,
                                                         DataReceivedCallback                          cb)
{
    Ptr session(new RemoteSession);
    session->data_->pipe = std::move(pipe);
    session->data_->cb = std::move(cb);

    return session;
}

void appbox::RemoteSession::Start()
{
    data_->WantRead();
}

void appbox::RemoteSession::Send(MsgPtr data)
{
    std::string header;
    std::string error;

    if (!MakeFrameHeader(data->size(), header, error))
    {
        SPDLOG_ERROR("[HANDLE: {}] {}", data_->pipe->native_handle(), error);
        return;
    }

    data_->send_queue.push_back(std::make_shared<std::string>(std::move(header)));
    data_->send_queue.push_back(std::move(data));

    data_->WantWrite();
}
