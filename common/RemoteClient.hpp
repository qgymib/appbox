#ifndef APPBOX_COMMON_REMOTE_CLIENT_HPP
#define APPBOX_COMMON_REMOTE_CLIENT_HPP

#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "RpcCodec.hpp"
#include "RemoteSession.hpp"

namespace appbox
{

/**
 * @brief Future of one pending RPC call.
 */
typedef std::future<RemoteResult> PipeResultFuture;

/**
 * @brief JSON-RPC client talking to a RemoteServer over a named pipe.
 */
class RemoteClient
{
public:
    typedef std::shared_ptr<RemoteClient> Ptr;

    virtual ~RemoteClient();

    /**
     * @brief Create a client for the given named pipe path.
     * @param[in] pipe_path Full path of the pipe.
     * @return The new client instance.
     */
    static Ptr Create(const std::string& pipe_path);

    /**
     * @brief Connect the client to the pipe and start the IO thread.
     * @return true when the connection succeeded, otherwise false.
     */
    bool Start();

    /**
     * @brief Call a method on the pipe server.
     * @param[in] method Method name to call.
     * @param[in] param Parameters for the method.
     * @return Response from the pipe server.
     */
    PipeResultFuture Call(const std::string& method, const nlohmann::json& param);

    /**
     * @brief Strong-typed RPC call.
     * @tparam[in] T Message type.
     * @param[in] req Request message
     * @return Response or error
     */
    template <typename T>
    std::future<tl::expected<typename T::Rsp, RemoteError>> Call(const typename T::Req& req)
    {
        /* Convert to json */
        nlohmann::json param = req;

        /* RPC call */
        auto future = Call(T::Method, param);

        /* Wrap */
        return std::async(
            std::launch::deferred,
            [fut = std::move(future)]() mutable -> tl::expected<typename T::Rsp, RemoteError> {
                auto result = fut.get();

                if (!result.has_value())
                {
                    return tl::unexpected(std::move(result.error()));
                }

                try
                {
                    return result.value().template get<typename T::Rsp>();
                }
                catch (const nlohmann::json::exception& e)
                {
                    return tl::unexpected(
                        RemoteError{ -1, std::string("Failed to deserialize response: ") + e.what(),
                                     result.value() });
                }
            });
    }

private:
    RemoteClient();

    /**
     * @brief Internal state of the client.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

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

        const uint64_t       id;      /* UID */
        nlohmann::json       req;     /* Request data */
        std::promise<RemoteResult> promise; /* Promise */
    };
    typedef std::map<uint64_t, PipeClientRequest::Ptr> RequestMap;

    Data()
        : uid_gen(0)
    {
    }

    ~Data()
    {
        io_context.stop();
        if (thread.joinable())
        {
            thread.join();
        }
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

        const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                              FORMAT_MESSAGE_IGNORE_INSERTS,
                                          nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                          (LPWSTR)&messageBuffer, 0, nullptr);

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
     * calls RemoteClient::Start().
     */
    void Connect()
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (size_t i = 0; i < kConnectRetries; ++i)
        {
            pipe = CreateFileA(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                break;
            }

            DWORD err = GetLastError();
            if (err != ERROR_PIPE_BUSY)
            {
                SPDLOG_ERROR(L"cannot open pipe: {}({})", GetErrorString(err), err);
                return;
            }

            if (!WaitNamedPipeA(pipe_path.c_str(), kConnectRetryTimeoutMs))
            {
                SPDLOG_ERROR("cannot open pipe");
                return;
            }
        }

        if (pipe == INVALID_HANDLE_VALUE)
        {
            /* Every attempt found the pipe busy, do not continue with no handle. */
            SPDLOG_ERROR("cannot open pipe: every attempt was busy");
            return;
        }

        DWORD mode = PIPE_READMODE_BYTE;
        if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
        {
            SPDLOG_ERROR("SetNamedPipeHandleState() failed: {}", GetLastError());
            CloseHandle(pipe);
            return;
        }

        std::shared_ptr<asio::windows::stream_handle> handle;
        try
        {
            handle = std::make_shared<asio::windows::stream_handle>(io_context, pipe);
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
     */
    void WorkThread()
    {
        io_context.run();
        SPDLOG_DEBUG("Thread exit");
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

    /* Number of attempts used to open the pipe of the server. */
    static constexpr size_t kConnectRetries = 5;

    /* Timeout of a single attempt to wait for a busy pipe instance. */
    static constexpr DWORD kConnectRetryTimeoutMs = 1000;

    std::atomic_uint64_t uid_gen;    /* UID generator */
    RemoteSession::Ptr   session;    /* RPC session */
    asio::io_context    io_context; /* IO context */
    std::string          pipe_path;  /* Named pipe path */
    std::thread          thread;     /* Work thread */

    RequestMap request_map;       /* Request map */
    std::mutex request_map_mutex; /* Request map mutex */
};

inline RemoteClient::RemoteClient()
    : data_(std::make_shared<Data>())
{
}

inline RemoteClient::~RemoteClient()
{
    data_.reset();
}

inline RemoteClient::Ptr RemoteClient::Create(const std::string& pipe_path)
{
    Ptr obj(new RemoteClient);
    obj->data_->pipe_path = pipe_path;
    return obj;
}

inline bool RemoteClient::Start()
{
    if (data_->session != nullptr)
    {
        SPDLOG_ERROR("RemoteClient already started");
        return false;
    }

    data_->Connect();

    if (data_->session == nullptr)
    {
        SPDLOG_ERROR("connect failed");
        return false;
    }

    data_->thread = std::thread(&Data::WorkThread, data_);
    return true;
}

inline PipeResultFuture RemoteClient::Call(const std::string& method, const nlohmann::json& param)
{
    if (data_->session == nullptr)
    {
        /* Fail fast instead of crashing on the null session later. */
        std::promise<RemoteResult> promise;
        promise.set_value(tl::unexpected<RemoteError>(
            RemoteError{ -1, "client is not started", nlohmann::json() }));
        return promise.get_future();
    }

    uint64_t id = data_->uid_gen++;
    auto     req = std::make_shared<Data::PipeClientRequest>(id);

    req->req["jsonrpc"] = "2.0";
    req->req["method"] = method;
    req->req["id"] = id;
    req->req["params"] = param;

    {
        std::lock_guard<std::mutex> lock(data_->request_map_mutex);
        data_->request_map.insert(Data::RequestMap::value_type(id, req));
    }

    auto msg = std::make_shared<std::string>(req->req.dump());

    auto ctx = data_;
    asio::post(data_->io_context, [ctx, msg]() { ctx->session->Send(msg); });

    return req->promise.get_future();
}

} // namespace appbox

#endif // APPBOX_COMMON_REMOTE_CLIENT_HPP
