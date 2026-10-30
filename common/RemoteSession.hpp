#ifndef APPBOX_COMMON_REMOTE_SESSION_HPP
#define APPBOX_COMMON_REMOTE_SESSION_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <windows.h>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include "RpcCodec.hpp"

namespace appbox
{

/**
 * @brief Budget of a strong-typed call which does not name one, in milliseconds.
 *
 * A call which names no budget still has to end: the peer may be gone without
 * closing the connection, and a caller which waited for ever would hang the
 * thread which runs the request.
 */
constexpr std::uint32_t kDefaultCallTimeoutMs = 5000;

/**
 * @brief Error code the strong-typed call reports for an answer it cannot read.
 *
 * The code is not one of the JSON-RPC codes: it describes the answer of a peer
 * which answered a payload the protocol of the request cannot be read from,
 * which is a defect of the two ends and no error of the method.
 */
constexpr int kResponseDecodeErrorCode = -1;

/**
 * @brief One framed JSON-RPC session over a connected named pipe handle.
 *
 * The session is the symmetric end of one connection: it sends requests and
 * waits for their answers, and it answers the requests its peer sends, so the
 * two ends of a pipe are interchangeable. The frame codec of `RpcCodec` is the
 * only thing the session knows about the transport; the owner of the handle
 * (an accepter or a connector) provides the IO thread the session runs on.
 *
 * The session keeps one table of the requests it sent and one table of the
 * requests it received, so a request of its peer is answered while a request of
 * its own is still pending. A request whose caller gave up is dropped from the
 * table, which makes a late answer harmless.
 */
class RemoteSession : public std::enable_shared_from_this<RemoteSession>
{
public:
    typedef std::shared_ptr<RemoteSession> Ptr;
    typedef std::shared_ptr<std::string>   MsgPtr;

    /**
     * @brief Handler of one method a peer may call.
     *
     * The handler runs on the IO thread of the session. It answers the request
     * through SendResponse() with the id it receives, and it must not block the
     * thread, because every other message of the connection waits for it.
     */
    typedef std::function<void(std::uint64_t, const nlohmann::json&)> MethodHandler;

    /**
     * @brief Handler which the session calls once its connection ended.
     *
     * The owner of a session which keeps it alive has to learn that the
     * connection is gone, because a session of a closed pipe answers nothing.
     * The handler runs on the IO thread of the owner, at most once, and must
     * neither block nor throw.
     */
    typedef std::function<void()> ClosedHandler;

    /**
     * @brief Create a session on top of a connected pipe handle.
     *
     * The handle has to be connected already: the session neither connects nor
     * waits for a peer, it starts reading when Start() is called.
     *
     * The session keeps the IO context of the handle alive, because the handle
     * refers to the service of that context: a caller which keeps a session
     * after its accepter or connector ended must not touch a context which is
     * gone.
     *
     * @param[in] pipe Connected pipe stream handle.
     * @param[in] peer_pid Process at the other end of the pipe, zero when it is
     *                     unknown.
     * @param[in] context IO context the handle runs on.
     * @return The new session instance.
     */
    static Ptr Create(std::shared_ptr<asio::windows::stream_handle> pipe, DWORD peer_pid,
                      std::shared_ptr<asio::io_context> context);

    ~RemoteSession();

    /**
     * @brief Set the largest request payload which is answered, in bytes.
     *
     * A request above the limit is answered with the invalid request error
     * instead of being parsed, so a peer cannot make the process allocate a
     * payload it did not agree to. The limit has to be set before Start().
     *
     * @param[in] max_request_size Largest payload which is accepted, in bytes.
     */
    void SetMaxRequestSize(std::size_t max_request_size);

    /**
     * @brief Set the handler which reports the end of the connection.
     *
     * The handler is called once, either when the peer closes the connection or
     * when the session is stopped, so an owner which keeps the session alive
     * releases it instead of holding a connection which serves nothing.
     *
     * @param[in] handler Handler to set, an empty function removes it.
     */
    void SetClosedHandler(ClosedHandler handler);

    /**
     * @brief Register the handler of one method.
     *
     * A method which is registered twice keeps its last handler. The call may be
     * made from any thread, including the IO thread of the session.
     *
     * @param[in] method Method name to register.
     * @param[in] handler Handler of the method.
     */
    void RegisterMethod(const std::string& method, MethodHandler handler);

    /**
     * @brief Answer one request which a handler of this session received.
     *
     * The answer carries the id of the request, which the session kept when it
     * dispatched the request. The call may be made from any thread.
     *
     * @param[in] request_id Id of the request, as the handler received it.
     * @param[in] result Result of the method or the error it reports.
     */
    void SendResponse(std::uint64_t request_id, const RemoteResult& result);

    /**
     * @brief Send a request and return the future of its answer.
     *
     * The caller decides how long it waits for the future. A request whose
     * answer never arrives keeps the entry of the table until the session ends,
     * so a caller which waits for ever also loses the memory of the entry.
     *
     * @param[in] method Method name to call.
     * @param[in] params Parameters of the method.
     * @return Future of the answer of the peer.
     */
    std::future<RemoteResult> Call(const std::string& method, const nlohmann::json& params);

    /**
     * @brief Send a request and wait a bounded time for its answer.
     *
     * The call blocks the calling thread. An answer which does not arrive in
     * time is reported as `ERROR_TIMEOUT` and the request is dropped, so an
     * answer which arrives afterwards is discarded without ending the session.
     *
     * @param[in] method Method name to call.
     * @param[in] params Parameters of the method.
     * @param[in] timeout_ms Budget of the wait.
     * @return Answer of the peer or the error of the call.
     */
    RemoteResult Call(const std::string& method, const nlohmann::json& params, std::uint32_t timeout_ms);

    /**
     * @brief Strong-typed request of one protocol struct.
     *
     * The request is the payload of the `params` member of the JSON-RPC request
     * and the answer is read as the `Rsp` of the protocol, so the two ends agree
     * on one type instead of on a JSON blob.
     *
     * @tparam[in] Protocol Protocol struct, see `common/RpcCodec.hpp`.
     * @param[in] request Request to send.
     * @param[in] timeout_ms Budget of the call, see Call().
     * @return The answer of the peer, or the error of the call.
     */
    template <typename Protocol>
    tl::expected<typename Protocol::Rsp, RemoteError> Call(const typename Protocol::Req& request,
                                                           std::uint32_t timeout_ms = kDefaultCallTimeoutMs)
    {
        const RemoteResult result = Call(Protocol::Method, nlohmann::json(request), timeout_ms);
        if (!result.has_value())
        {
            return tl::unexpected<RemoteError>(result.error());
        }

        try
        {
            return result.value().template get<typename Protocol::Rsp>();
        }
        catch (const nlohmann::json::exception& e)
        {
            return tl::unexpected<RemoteError>(RemoteError{
                kResponseDecodeErrorCode, std::string("Failed to deserialize response: ") + e.what(), result.value() });
        }
    }

    /**
     * @brief Process at the other end of the connection.
     * @return Process id of the peer, zero when it is unknown.
     */
    DWORD PeerProcessId() const;

    /**
     * @brief Query the process at the other end of the session of a request.
     *
     * The request has to be answered after the query, because the answer
     * releases the entry the query reads.
     *
     * @param[in] request_id Id of a request this session received.
     * @return Process id of the peer, zero when it is unknown.
     */
    DWORD PeerProcessIdOfRequest(std::uint64_t request_id) const;

    /**
     * @brief Start the asynchronous read loop of the session.
     */
    void Start();

    /**
     * @brief Send one message, the frame header is prepended transparently.
     *
     * The payload is written as it is, which is what a test needs to send a
     * payload which is no request. A caller which sends a request of its own
     * uses Call() instead, which keeps the request table.
     *
     * @param[in] data Message payload.
     */
    void Send(MsgPtr data);

    /**
     * @brief Close the pipe of the session.
     *
     * The pending reads and writes complete with an error, so the owner of the
     * session learns that the connection is gone. The call may be repeated.
     */
    void Close();

    /**
     * @brief Wait until the messages which are queued for the peer are written.
     * @param[in] timeout_ms Upper bound of the wait.
     * @return true when nothing is pending any more, otherwise false.
     */
    bool WaitForPendingWrites(std::uint32_t timeout_ms);

    /**
     * @brief Wait until every request this session received is answered.
     *
     * The owner of the session waits with the call before it closes the
     * connection, so an answer which a handler still produces reaches its peer.
     *
     * @param[in] timeout_ms Upper bound of the wait.
     * @return true when nothing is pending any more, otherwise false.
     */
    bool WaitForIncomingResponses(std::uint32_t timeout_ms);

    /**
     * @brief End the session and fail the requests which still wait.
     *
     * Every caller which waits for an answer is released with an error instead
     * of waiting for ever, and the connection is closed. The call is idempotent.
     */
    void Stop();

private:
    RemoteSession();

    /**
     * @brief Internal state of the session.
     */
    struct Data;

    std::shared_ptr<Data> data_;
};

} // namespace appbox

/*
 * The internal state of the session and the members of the class are its
 * implementation, so they live in a detail header which only this header
 * includes.
 */
#include "details/RemoteSessionDetail.hpp"

#endif // APPBOX_COMMON_REMOTE_SESSION_HPP
