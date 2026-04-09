#include <spdlog/spdlog.h>
#include <atomic>
#include <utility>
#include <map>
#include <mutex>
#include "RemoteSession.hpp"
#include "RemoteClient.hpp"

struct PipeClientRequest
{
    typedef std::shared_ptr<PipeClientRequest> Ptr;

    PipeClientRequest(uint64_t id) : id(id)
    {
    }

    const uint64_t                   id;      /* UID */
    nlohmann::json                   req;     /* Request data. */
    std::promise<appbox::RemoteResult> promise; /* Promise */
};
typedef std::map<uint64_t, PipeClientRequest::Ptr> RequestMap;

struct appbox::RemoteClient::Data : std::enable_shared_from_this<Data>
{
    Data();
    ~Data();

    void ConnectThread();
    void WorkThread();
    void OnRecv(const asio::error_code&, RemoteSession::MsgPtr);

    std::atomic_uint64_t uid_gen;    /* UID generator. */
    RemoteSession::Ptr     session;    /* RPC session. */
    asio::io_context     io_context; /* IO context */
    std::string          pipe_path;  /* Named pipe path. */
    std::thread*         thread;     /* Work thread */

    RequestMap request_map;       /* Request map */
    std::mutex request_map_mutex; /* Request map mutex */
};

appbox::RemoteClient::Data::Data() : uid_gen(0), thread(nullptr)
{
}

appbox::RemoteClient::Data::~Data()
{
    io_context.stop();
    if (thread != nullptr)
    {
        thread->join();
        delete thread;
        thread = nullptr;
    }
}

static std::wstring GetErrorString(DWORD errorMessageID)
{
    if (errorMessageID == 0)
    {
        return L"";
    }

    LPWSTR messageBuffer = nullptr;

    size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPWSTR)&messageBuffer, 0, nullptr);

    std::wstring message(messageBuffer, size);

    LocalFree(messageBuffer);

    return message;
}

void appbox::RemoteClient::Data::ConnectThread()
{
    HANDLE pipe = nullptr;
    for (size_t i = 0; i < 5; ++i)
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

        if (!WaitNamedPipeA(pipe_path.c_str(), 1 * 1000))
        {
            SPDLOG_ERROR("cannot open pipe");
            return;
        }
    }

    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
    {
        SPDLOG_ERROR("SetNamedPipeHandleState() failed: {}", GetLastError());
        return;
    }

    auto handle = std::make_shared<asio::windows::stream_handle>(io_context, pipe);

    auto self = shared_from_this();
    session =
        RemoteSession::Create(handle, [self](const asio::error_code& ec, RemoteSession::MsgPtr msg) {
            self->OnRecv(ec, std::move(msg));
        });
    session->Start();
}

void appbox::RemoteClient::Data::WorkThread()
{
    io_context.run();
    SPDLOG_DEBUG("Thread exit");
}

void appbox::RemoteClient::Data::OnRecv(const asio::error_code& ec, RemoteSession::MsgPtr msg)
{
    if (ec)
    {
        SPDLOG_ERROR("PipeClient recv failed: {}", ec.message());
        return;
    }

    nlohmann::json j_rsp = nlohmann::json::parse(*msg);
    uint64_t       id = j_rsp["id"].get<uint64_t>();

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

    auto it_result = j_rsp.find("result");
    if (it_result != j_rsp.end())
    {
        orig_req->promise.set_value(*it_result);
        return;
    }

    nlohmann::json j_err = j_rsp["error"];
    auto err = tl::unexpected<RemoteError>({ j_err["code"].get<int>(), j_err.value("message", ""),
                                           j_err.value("data", nlohmann::json::object()) });
    orig_req->promise.set_value(err);
}

appbox::RemoteClient::RemoteClient()
{
    data_ = std::make_shared<Data>();
}

appbox::RemoteClient::~RemoteClient()
{
    data_.reset();
}

appbox::RemoteClient::Ptr appbox::RemoteClient::Create(const std::string& pipe_path)
{
    Ptr obj(new RemoteClient);
    obj->data_->pipe_path = pipe_path;
    return obj;
}

bool appbox::RemoteClient::Start()
{
    if (data_->session.get() != nullptr)
    {
        SPDLOG_ERROR("PipeClient already started");
        return false;
    }

    data_->thread = new std::thread(&Data::ConnectThread, data_);
    data_->thread->join();
    delete data_->thread;
    data_->thread = nullptr;
    if (data_->session.get() == nullptr)
    {
        SPDLOG_ERROR("connect failed");
        return false;
    }

    data_->thread = new std::thread(&Data::WorkThread, data_);
    return true;
}

appbox::PipeResultFuture appbox::RemoteClient::Call(const std::string&    method,
                                                  const nlohmann::json& param)
{
    uint64_t id = data_->uid_gen++;
    auto     req = std::make_shared<PipeClientRequest>(id);

    req->req["jsonrpc"] = "2.0";
    req->req["method"] = method;
    req->req["id"] = id;
    req->req["params"] = param;

    {
        std::lock_guard<std::mutex> lock(data_->request_map_mutex);
        data_->request_map.insert(RequestMap::value_type(id, req));
    }

    auto msg = std::make_shared<std::string>(req->req.dump());

    auto ctx = data_;
    asio::post(data_->io_context, [ctx, msg]() { ctx->session->Send(msg); });

    return req->promise.get_future();
}
