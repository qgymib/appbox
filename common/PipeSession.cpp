#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <windows.h>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <list>
#include <array>
#include "PipeSession.hpp"

struct appbox::PipeSession::Data : std::enable_shared_from_this<Data>
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

appbox::PipeSession::Data::Data() : in_write(false)
{
}

void appbox::PipeSession::Data::WantWrite()
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
            SPDLOG_ERROR("write pipe failed: {}", ec.message());
            return;
        }

        self->WantWrite();
    });
}

void appbox::PipeSession::Data::WantRead()
{
    auto self = shared_from_this();
    auto fn = [self](const asio::error_code& ec, std::size_t n) {
        if (ec)
        {
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

    asio::async_read(*pipe, asio::buffer(recv_buff), fn);
}

void appbox::PipeSession::Data::OnRead()
{
    if (recv_data.size() < sizeof(PipeProtocol))
    {
        return;
    }

    PipeProtocol head;
    memcpy(&head, recv_data.data(), sizeof(head));
    if (head.magic != PipeProtocol().magic)
    {
        throw std::runtime_error("invalid magic");
    }

    size_t total_msg_bytes = head.length + sizeof(head);
    if (total_msg_bytes > recv_data.size())
    { /* no enough data */
        return;
    }
    size_t left_bytes = recv_data.size() - total_msg_bytes;

    auto p_start = reinterpret_cast<const char*>(recv_data.data() + sizeof(head));
    auto msg = std::make_shared<std::string>(p_start, head.length);
    memmove(recv_data.data(), recv_data.data() + total_msg_bytes, left_bytes);
    recv_data.resize(left_bytes);

    cb(asio::error_code(), msg);
}

appbox::PipeSession::PipeSession()
{
    data_ = std::make_shared<Data>();
}

appbox::PipeSession::~PipeSession()
{
    data_.reset();
}

appbox::PipeSession::Ptr appbox::PipeSession::Create(
    std::shared_ptr<asio::windows::stream_handle> pipe, DataReceivedCallback cb)
{
    Ptr session(new PipeSession);
    session->data_->pipe = std::move(pipe);
    session->data_->cb = std::move(cb);

    return session;
}

void appbox::PipeSession::Start()
{
    data_->WantRead();
}

void appbox::PipeSession::Send(MsgPtr data)
{
    PipeProtocol head;
    head.length = data->size();

    auto p_head = std::make_shared<std::string>(reinterpret_cast<const char*>(&head), sizeof(head));
    data_->send_queue.push_back(std::move(p_head));
    data_->send_queue.push_back(std::move(data));

    data_->WantWrite();
}
