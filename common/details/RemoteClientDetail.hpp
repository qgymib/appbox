#ifndef APPBOX_COMMON_DETAILS_REMOTE_CLIENT_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_CLIENT_DETAIL_HPP

/*
 * Implementation of `common/RemoteClient.hpp`: the internal state of the client,
 * the connection of the pipe and the members of the class. The header is
 * included by the public header and is not meant to be included on its own.
 */

#include "RemoteClient.hpp"
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace appbox
{
namespace detail
{

/**
 * @brief Whether a failed connection to a pipe is worth another attempt.
 *
 * A server which is still starting has not created its pipe yet, so the name is
 * unknown; a server which replaces its instance leaves the pipe momentarily
 * busy, unconnected or broken.
 *
 * @param[in] error Error code of CreateFile().
 * @return true when the connection should be retried.
 */
inline bool IsRetryableRemoteClientConnectError(DWORD error)
{
    switch (error)
    {
    case ERROR_PIPE_BUSY:
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_BROKEN_PIPE:
        return true;
    default:
        return false;
    }
}

} // namespace detail

/**
 * @brief Internal state of one RemoteClient.
 */
struct RemoteClient::Data : std::enable_shared_from_this<Data>
{
    /**
     * @brief One request waiting for its response.
     */
    struct PipeClientRequest
    {
        typedef std::shared_ptr<PipeClientRequest> Ptr;

        PipeClientRequest(uint64_t id) : id(id)
        {
        }

        const uint64_t             id;      /* UID */
        nlohmann::json             req;     /* Request data */
        std::promise<RemoteResult> promise; /* Promise */
    };
    typedef std::map<uint64_t, PipeClientRequest::Ptr> RequestMap;

    Data() : uid_gen(0), io_context(std::make_shared<asio::io_context>()), server_pid(0)
    {
    }

    ~Data()
    {
        Stop();
    }

    /**
     * @brief End the IO thread and release the handlers which are still queued.
     *
     * The call is idempotent, so the destructor may repeat it.
     */
    void Stop()
    {
        if (io_context)
        {
            io_context->stop();
        }

        if (thread.joinable())
        {
            if (std::this_thread::get_id() == thread.get_id())
            {
                thread.detach();
            }
            else
            {
                thread.join();
            }
        }

        io_context.reset();
    }

    /**
     * @brief Describe a Windows error code.
     * @param[in] error Error code returned by GetLastError().
     * @return The system description of the error, empty when not available.
     */
    static std::wstring GetErrorString(DWORD error)
    {
        if (error == 0)
        {
            return L"";
        }

        LPWSTR messageBuffer = nullptr;

        const DWORD size = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, nullptr);

        if (size == 0 || messageBuffer == nullptr)
        {
            /* No description is available for this error code. */
            return L"";
        }

        std::wstring message(messageBuffer, size);
        LocalFree(messageBuffer);

        return message;
    }

    /**
     * @brief Open the pipe and start the session.
     *
     * The connect is a short blocking operation, it runs on the thread which
     * calls RemoteClient::Start(). A pipe which does not exist yet or which is
     * busy is retried with a growing delay until the budget is used up.
     *
     * @param[in] timeout_ms Budget of the connection attempts.
     */
    void Connect(uint32_t timeout_ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        uint32_t   delay_ms = kFirstRetryDelayMs;

        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (;;)
        {
            pipe = CreateFileA(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                break;
            }

            const DWORD err = GetLastError();
            if (!detail::IsRetryableRemoteClientConnectError(err))
            {
                SPDLOG_ERROR(L"cannot open pipe: {}({})", GetErrorString(err), err);
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                SPDLOG_ERROR("cannot open pipe: the budget of {} ms is used up", timeout_ms);
                return;
            }

            auto wait_ms =
                static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            if (wait_ms > delay_ms)
            {
                wait_ms = delay_ms;
            }
            if (wait_ms == 0)
            {
                wait_ms = 1;
            }

            if (err == ERROR_PIPE_BUSY)
            {
                WaitNamedPipeA(pipe_path.c_str(), wait_ms);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            }

            delay_ms = (delay_ms * 2 > kMaxRetryDelayMs) ? kMaxRetryDelayMs : delay_ms * 2;
        }

        DWORD mode = PIPE_READMODE_BYTE;
        if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
        {
            SPDLOG_ERROR("SetNamedPipeHandleState() failed: {}", GetLastError());
            CloseHandle(pipe);
            return;
        }

        ULONG server_process_id = 0;
        if (GetNamedPipeServerProcessId(pipe, &server_process_id))
        {
            server_pid = server_process_id;
        }
        else
        {
            SPDLOG_WARN("cannot query the process of the server: {}", GetLastError());
        }

        std::shared_ptr<asio::windows::stream_handle> handle;
        try
        {
            handle = std::make_shared<asio::windows::stream_handle>(*io_context, pipe);
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("failed to create the pipe stream: {}", e.what());
            CloseHandle(pipe);
            return;
        }

        auto self = shared_from_this();
        session = RemoteSession::Create(handle, [self](const asio::error_code& ec, RemoteSession::MsgPtr msg) {
            self->OnRecv(ec, std::move(msg));
        });
        session->Start();
    }

    /**
     * @brief Entry point of the IO thread.
     *
     * The thread does not log when it ends: a client may be destroyed by a
     * static destructor of the process, which runs after the logger of the
     * process is gone.
     *
     * @param[in] context IO context of the client.
     */
    static void WorkThread(std::shared_ptr<asio::io_context> context)
    {
        context->run();
    }

    /**
     * @brief Parse one response message and deliver it to the waiting caller.
     * @param[in] ec Error code of the receive.
     * @param[in] msg Message payload.
     */
    void OnRecv(const asio::error_code& ec, RemoteSession::MsgPtr msg)
    {
        if (ec)
        {
            SPDLOG_ERROR("RemoteClient recv failed: {}", ec.message());
            FailPending(ec);
            return;
        }

        nlohmann::json rsp;
        try
        {
            rsp = nlohmann::json::parse(*msg);
        }
        catch (const nlohmann::json::exception& e)
        {
            SPDLOG_ERROR("failed to parse the response: {}", e.what());
            return;
        }

        uint64_t     id = 0;
        RemoteResult result;
        std::string  error;
        if (!ParseRpcResponse(rsp, id, result, error))
        {
            SPDLOG_ERROR("invalid response: {}", error);
            return;
        }

        PipeClientRequest::Ptr orig_req;
        {
            std::lock_guard<std::mutex> lock(request_map_mutex);
            auto                        it = request_map.find(id);
            if (it == request_map.end())
            {
                SPDLOG_ERROR("request id {} not found", id);
                return;
            }
            orig_req = it->second;
            request_map.erase(it);
        }

        try
        {
            orig_req->promise.set_value(std::move(result));
        }
        catch (const std::future_error& e)
        {
            SPDLOG_ERROR("failed to deliver the response of request {}: {}", id, e.what());
        }
    }

    /**
     * @brief Answer every pending request with the error which ended the link.
     *
     * The connection of the client is its only link to the server, so a
     * response cannot arrive any more and a caller must not wait forever.
     *
     * @param[in] ec Error which ended the link.
     */
    void FailPending(const asio::error_code& ec)
    {
        RequestMap pending;
        {
            std::lock_guard<std::mutex> lock(request_map_mutex);
            pending.swap(request_map);
        }

        for (auto& item : pending)
        {
            try
            {
                item.second->promise.set_value(tl::unexpected<RemoteError>(
                    RemoteError{ ec.value(), std::string("the connection to the pipe server ended: ") + ec.message(),
                                 nlohmann::json() }));
            }
            catch (const std::future_error& e)
            {
                SPDLOG_ERROR("failed to deliver the failure of request {}: {}", item.first, e.what());
            }
        }
    }

    /**
     * @brief Queue one request and return the future of its response.
     * @param[in] method Method name.
     * @param[in] param Parameters of the method.
     * @param[out] out_id Id which identifies the request.
     * @return Future of the response.
     */
    std::future<RemoteResult> Send(const std::string& method, const nlohmann::json& param, uint64_t& out_id)
    {
        if (session == nullptr || !io_context)
        {
            /* Fail fast instead of crashing on the null session later. */
            std::promise<RemoteResult> promise;
            promise.set_value(
                tl::unexpected<RemoteError>(RemoteError{ -1, "client is not started", nlohmann::json() }));
            return promise.get_future();
        }

        const uint64_t id = uid_gen++;
        out_id = id;

        auto req = std::make_shared<PipeClientRequest>(id);

        req->req["jsonrpc"] = "2.0";
        req->req["method"] = method;
        req->req["id"] = id;
        req->req["params"] = param;

        {
            std::lock_guard<std::mutex> lock(request_map_mutex);
            request_map.insert(RequestMap::value_type(id, req));
        }

        auto msg = std::make_shared<std::string>(req->req.dump());

        auto ctx = shared_from_this();
        asio::post(*io_context, [ctx, msg]() { ctx->session->Send(msg); });

        return req->promise.get_future();
    }

    /**
     * @brief Drop a request whose response is not awaited any more.
     * @param[in] id Id of the request.
     */
    void Forget(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(request_map_mutex);
        request_map.erase(id);
    }

    /* Number of the first delay between two connection attempts, in milliseconds. */
    static constexpr uint32_t kFirstRetryDelayMs = 20;

    /* Upper bound of the delay between two connection attempts, in milliseconds. */
    static constexpr uint32_t kMaxRetryDelayMs = 200;

    std::atomic_uint64_t              uid_gen;    /* UID generator */
    RemoteSession::Ptr                session;    /* RPC session */
    std::shared_ptr<asio::io_context> io_context; /* IO context */
    std::string                       pipe_path;  /* Named pipe path */
    std::thread                       thread;     /* Work thread */
    DWORD                             server_pid; /* Process of the server */

    RequestMap request_map;       /* Request map */
    std::mutex request_map_mutex; /* Request map mutex */
};

/* Definition of the contract on the declaration. */
inline RemoteClient::RemoteClient() : data_(std::make_shared<Data>())
{
}

/* Definition of the contract on the declaration. */
inline RemoteClient::~RemoteClient()
{
    if (data_)
    {
        data_->Stop();
    }
    data_.reset();
}

/* Definition of the contract on the declaration. */
inline RemoteClient::Ptr RemoteClient::Create(const std::string& pipe_path)
{
    Ptr obj(new RemoteClient);
    obj->data_->pipe_path = pipe_path;
    return obj;
}

/* Definition of the contract on the declaration. */
inline bool RemoteClient::Start(uint32_t timeout_ms)
{
    if (data_->session != nullptr)
    {
        SPDLOG_ERROR("RemoteClient already started");
        return false;
    }

    data_->Connect(timeout_ms);

    if (data_->session == nullptr)
    {
        SPDLOG_ERROR("connect failed");
        return false;
    }

    data_->thread = std::thread(&Data::WorkThread, data_->io_context);
    return true;
}

/* Definition of the contract on the declaration. */
inline PipeResultFuture RemoteClient::Call(const std::string& method, const nlohmann::json& param)
{
    uint64_t id = 0;
    return data_->Send(method, param, id);
}

/* Definition of the contract on the declaration. */
inline RemoteResult RemoteClient::Call(const std::string& method, const nlohmann::json& param, uint32_t timeout_ms)
{
    uint64_t id = 0;
    auto     future = data_->Send(method, param, id);

    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready)
    {
        data_->Forget(id);

        return tl::unexpected<RemoteError>(
            RemoteError{ static_cast<int>(ERROR_TIMEOUT), "the pipe server did not answer in time", nlohmann::json() });
    }

    return future.get();
}

/* Definition of the contract on the declaration. */
inline DWORD RemoteClient::ServerProcessId() const
{
    return data_->server_pid;
}

} // namespace appbox

#endif
