#ifndef APPBOX_COMMON_REMOTE_CLIENT_HPP
#define APPBOX_COMMON_REMOTE_CLIENT_HPP

#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdint>
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
     *
     * A pipe which does not exist yet, which is busy or which was just closed
     * is retried until the budget is used up, so a client may start while the
     * server is still starting.
     *
     * @param[in] timeout_ms Budget of the connection attempts.
     * @return true when the connection succeeded, otherwise false.
     */
    bool Start(uint32_t timeout_ms = 5000);

    /**
     * @brief Call a method on the pipe server.
     * @param[in] method Method name to call.
     * @param[in] param Parameters for the method.
     * @return Response from the pipe server.
     */
    PipeResultFuture Call(const std::string& method, const nlohmann::json& param);

    /**
     * @brief Call a method and wait a bounded time for its response.
     *
     * The call blocks the calling thread. A response which does not arrive in
     * time is reported as ERROR_TIMEOUT and the pending request is dropped.
     *
     * @param[in] method Method name to call.
     * @param[in] param Parameters for the method.
     * @param[in] timeout_ms Budget of the wait.
     * @return Response from the pipe server or the error of the call.
     */
    RemoteResult Call(const std::string& method, const nlohmann::json& param, uint32_t timeout_ms);

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
            std::launch::deferred, [fut = std::move(future)]() mutable -> tl::expected<typename T::Rsp, RemoteError> {
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
                        RemoteError{ -1, std::string("Failed to deserialize response: ") + e.what(), result.value() });
                }
            });
    }

    /**
     * @brief Process id of the server at the other end of the pipe.
     * @return Process id of the server, zero when it is unknown.
     */
    DWORD ServerProcessId() const;

private:
    RemoteClient();

    /**
     * @brief Internal state of the client.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

} // namespace appbox

/*
 * The internal state of the client and the members of the class are its
 * implementation, so they live in a detail header which only this header
 * includes.
 */
#include "details/RemoteClientDetail.hpp"

#endif // APPBOX_COMMON_REMOTE_CLIENT_HPP
