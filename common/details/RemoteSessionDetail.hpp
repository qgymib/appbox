#ifndef APPBOX_COMMON_DETAILS_REMOTE_SESSION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_SESSION_DETAIL_HPP

/*
 * Implementation of `common/RemoteSession.hpp`: the internal state of the
 * session, the thread which completes the overlapped reads and writes of its
 * pipe, the two request tables and the members of the class. The header is
 * included by the public header and is not meant to be included on its own.
 */

#include "RemoteSession.hpp"
#include "RemoteThread.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace appbox
{

/**
 * @brief Internal state of one RemoteSession.
 */
struct RemoteSession::Data : std::enable_shared_from_this<Data>
{
    /**
     * @brief One request this session sent and whose answer is still missing.
     */
    struct PendingRequest
    {
        typedef std::shared_ptr<PendingRequest> Ptr;

        explicit PendingRequest(std::uint64_t id) : id(id)
        {
        }

        const std::uint64_t        id;      /* Id the request carries on the wire */
        std::promise<RemoteResult> promise; /* Promise of the answer */
    };

    /**
     * @brief One request a peer sent and whose answer is still missing.
     */
    struct IncomingRequest
    {
        typedef std::shared_ptr<IncomingRequest> Ptr;

        IncomingRequest(std::uint64_t uid, const nlohmann::json& id) : uid(uid), id(id)
        {
        }

        const std::uint64_t  uid; /* Id the handler answers with */
        const nlohmann::json id;  /* Id the request carried on the wire */
    };

    typedef std::map<std::uint64_t, PendingRequest::Ptr>  PendingRequestMap;
    typedef std::map<std::uint64_t, IncomingRequest::Ptr> IncomingRequestMap;
    typedef std::map<std::string, MethodHandler>          MethodHandlerMap;

    /**
     * @brief Create the events the thread of the session waits on.
     *
     * The events are automatic reset events: a wait consumes the signal, so the
     * thread which signals one never has to know whether the thread of the
     * session already noticed the previous one.
     *
     * The state does not report the failure itself: the factory of the session
     * asks with HasEvents() and answers the code, so a state which was built
     * without its events is never handed out.
     */
    Data()
    {
        read_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        write_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        write_queued_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stop_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    /**
     * @brief Whether every event the thread waits on was created.
     * @return true when the session can serve its connection.
     */
    bool HasEvents() const noexcept
    {
        return read_event != nullptr && write_event != nullptr && write_queued_event != nullptr &&
               stop_event != nullptr;
    }

    ~Data()
    {
        Stop();

        /* The thread ended above, so no completion of the pipe uses them any more. */
        detail::CloseHandleIfValid(read_event);
        detail::CloseHandleIfValid(write_event);
        detail::CloseHandleIfValid(write_queued_event);
        detail::CloseHandleIfValid(stop_event);
    }

    /**
     * @brief The loop of the thread which serves the connection.
     *
     * One thread completes both directions: it waits on the event of the read,
     * on the event which reports that a message was queued, on the event of the
     * write and on the stop event, so the reads and the writes of the
     * connection are serialized without a lock and a caller which queues a
     * message never waits for the pipe.
     *
     * A read which ends the connection leaves the loop through
     * OnConnectionEnded(); a stop ends it through the stop event, which the
     * owner sets with Close() or Stop().
     */
    void Run()
    {
        if (!PostRead())
        {
            CloseHandleNow();
            return;
        }

        for (;;)
        {
            const HANDLE handles[kWaitCount] = { read_event, write_queued_event, write_event, stop_event };
            const DWORD  index = ::WaitForMultipleObjects(kWaitCount, handles, FALSE, INFINITE);

            if (index == detail::kWaitObjectFirst + 3)
            { /* The owner stopped the session. */
                break;
            }

            if (index == detail::kWaitObjectFirst)
            {
                if (!OnReadCompleted())
                {
                    break;
                }
                continue;
            }

            if (index == detail::kWaitObjectFirst + 1)
            { /* A caller queued a message. */
                StartNextWrite();
                continue;
            }

            if (index == detail::kWaitObjectFirst + 2)
            {
                if (!OnWriteCompleted())
                {
                    break;
                }
                continue;
            }

            /* The wait itself failed, which only a broken handle causes. */
            OnConnectionEnded(static_cast<int>(ERROR_INVALID_HANDLE), "the wait for the pipe of the session failed");
            break;
        }

        CloseHandleNow();
    }

    /**
     * @brief Issue the next overlapped read into the receive buffer.
     * @return true when a read is in flight or completed, otherwise false after
     *         the connection was reported as ended.
     */
    bool PostRead()
    {
        const HANDLE pipe = Handle();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        std::memset(&read_ov, 0, sizeof(read_ov));
        read_ov.hEvent = read_event;

        DWORD transferred = 0;
        if (::ReadFile(pipe, recv_buff.data(), static_cast<DWORD>(recv_buff.size()), &transferred, &read_ov))
        {
            /* The read completed at once, its event reports the completion. */
            return true;
        }

        const DWORD error = ::GetLastError();
        if (error == ERROR_IO_PENDING)
        {
            return true;
        }

        OnConnectionEnded(static_cast<int>(error), "the read of the pipe failed");
        return false;
    }

    /**
     * @brief Consume one completed read and start the next one.
     * @return true when the session keeps serving, false when the connection
     *         ended.
     */
    bool OnReadCompleted()
    {
        const HANDLE pipe = Handle();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD transferred = 0;
        if (!::GetOverlappedResult(pipe, &read_ov, &transferred, FALSE))
        {
            const DWORD error = ::GetLastError();
            OnConnectionEnded(static_cast<int>(error), "the connection to the peer ended");
            return false;
        }

        if (transferred == 0)
        {
            /* A pipe which delivers no byte at all was closed by the peer. */
            OnConnectionEnded(static_cast<int>(ERROR_BROKEN_PIPE), "the peer closed the connection");
            return false;
        }

        const size_t old_sz = recv_data.size();
        const size_t new_sz = old_sz + transferred;
        recv_data.resize(new_sz);
        std::memcpy(recv_data.data() + old_sz, recv_buff.data(), transferred);

        OnRead();

        /*
         * The answers which the handlers queued leave before the next read is
         * issued, so a peer which floods the connection cannot starve them: the
         * write is overlapped and runs while the next read is pending.
         */
        StartNextWrite();

        return PostRead();
    }

    /**
     * @brief Drain the receive buffer and report every complete frame.
     */
    void OnRead()
    {
        /*
         * One read may deliver several frames at once or a part of the next
         * frame, so the buffer is drained until it does not hold a complete
         * frame any more.
         */
        for (;;)
        {
            const auto decoded = TryDecodeFrame(recv_data);
            if (!decoded.has_value())
            {
                /*
                 * A frame which is no frame of this protocol cannot be skipped:
                 * the stream holds no position to resynchronise at, so the
                 * connection is dropped and the code names the reason.
                 */
                OnConnectionEnded(static_cast<int>(ERROR_INVALID_DATA),
                                  "the peer sent an invalid frame: " +
                                      std::to_string(static_cast<int>(decoded.error())),
                                  kResponseDecodeErrorCode);
                return;
            }

            if (decoded->status == FrameStatus::NeedMoreData)
            {
                return;
            }

            OnMessage(decoded->payload);
        }
    }

    /**
     * @brief Report that the connection ended and release the waiting callers.
     *
     * The connection of a session is its only link to the peer, so an answer
     * cannot arrive any more: the pending requests are failed and the pipe is
     * closed, which ends the loop of the thread.
     *
     * The code a caller receives describes why the connection ended and never
     * depends on the peer alone: a pipe the peer closed, a pipe whose read or
     * write failed and a session its owner stopped all report
     * `kSessionClosedErrorCode`, while a payload which is no frame of this
     * protocol reports `kResponseDecodeErrorCode`, because the peer of that
     * payload answered something this end cannot read.
     *
     * @param[in] code Code of the failure, as the operating system or the frame
     *                 decoder reported it.
     * @param[in] message Description of the failure.
     * @param[in] report The code a waiting caller reads instead of the failure
     *                 code of the transport.
     */
    void OnConnectionEnded(int code, const std::string& message, int report = kSessionClosedErrorCode)
    {
        if (ended.exchange(true))
        {
            return;
        }

        FailPending(report, message + " (transport code " + std::to_string(code) + ")");
        Close();
        NotifyClosed();
    }

    /**
     * @brief Report the end of the connection to the owner of the session.
     *
     * The handler is released before it is called, so a session which the
     * handler destroys cannot report the same end twice.
     */
    void NotifyClosed()
    {
        ClosedHandler handler;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            handler.swap(closed_handler);
        }

        if (!handler)
        {
            return;
        }

        /* The owner reports its own failures; nothing is thrown on this thread. */
        try
        {
            handler();
        }
        catch (...)
        {
        }
    }

    /**
     * @brief Classify one received payload and handle it.
     *
     * A payload which carries a method is a request of the peer and is
     * dispatched, a payload which carries a result or an error is the answer of
     * a request of this session, and everything else is answered as a malformed
     * request. Nothing is thrown out of the loop of the thread.
     *
     * @param[in] payload Payload of one decoded frame.
     */
    void OnMessage(const std::string& payload)
    {
        if (payload.size() > max_request_size)
        {
            AnswerError(nlohmann::json(), -32600, "Invalid Request");
            return;
        }

        nlohmann::json message;
        try
        {
            message = nlohmann::json::parse(payload);
        }
        catch (const std::exception&)
        {
            AnswerError(nlohmann::json(), -32700, "Parse error");
            return;
        }

        if (!message.is_object())
        {
            AnswerError(nlohmann::json(), -32600, "Invalid Request");
            return;
        }

        const auto it_id = message.find("id");
        const auto it_method = message.find("method");

        if (it_method == message.end())
        {
            if (message.contains("result") || message.contains("error"))
            {
                OnAnswer(message);
                return;
            }

            AnswerError(it_id != message.end() ? *it_id : nlohmann::json(), -32600, "Invalid Request");
            return;
        }

        if (!it_method->is_string() || it_id == message.end())
        {
            AnswerError(it_id != message.end() ? *it_id : nlohmann::json(), -32600, "Invalid Request");
            return;
        }

        DispatchRequest(it_method->get<std::string>(), *it_id, message);
    }

    /**
     * @brief Dispatch one request of the peer to the registered handler.
     *
     * The handler is copied under the lock of the method table and called
     * without it, so a handler which registers another method cannot deadlock.
     *
     * @param[in] method Method the request names.
     * @param[in] id Id the request carries.
     * @param[in] message The whole request object.
     */
    void DispatchRequest(const std::string& method, const nlohmann::json& id, const nlohmann::json& message)
    {
        MethodHandler handler;
        {
            std::lock_guard<std::mutex> lock(method_mutex);
            const auto                  it = method_handlers.find(method);
            if (it == method_handlers.end())
            {
                handler = MethodHandler();
            }
            else
            {
                handler = it->second;
            }
        }

        if (!handler)
        {
            AnswerError(id, -32601, "Method not found");
            return;
        }

        const std::uint64_t uid = uid_gen++;

        {
            std::lock_guard<std::mutex> lock(request_mutex);
            incoming_requests.emplace(uid, std::make_shared<IncomingRequest>(uid, id));
            ++inflight;
        }

        const auto           it_params = message.find("params");
        const nlohmann::json param = (it_params == message.end()) ? nlohmann::json(nullptr) : *it_params;

        /*
         * A handler which throws answers with the internal error, so the peer
         * is released instead of waiting for an answer which never comes.
         */
        try
        {
            handler(uid, param);
        }
        catch (...)
        {
            /* The answer of this end is small, so its outcome is not reported. */
            (void)SendResponse(uid,
                               tl::unexpected<RemoteError>(RemoteError{ -32603, "Internal error", nlohmann::json() }));
        }
    }

    /**
     * @brief Deliver one answer to the caller which waits for it.
     *
     * An answer whose request is not in the table belongs to a caller which gave
     * up when its budget was used up: it is dropped, and the connection stays
     * usable for every other request.
     *
     * @param[in] message The whole answer object.
     */
    void OnAnswer(const nlohmann::json& message)
    {
        const auto parsed = ParseRpcResponse(message);
        if (!parsed.has_value())
        {
            /*
             * An answer whose request cannot be read names no entry of the
             * table, so it is discarded the way a late answer is discarded.
             */
            return;
        }

        PendingRequest::Ptr pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            const auto                  it = pending_requests.find(parsed->id);
            if (it == pending_requests.end())
            {
                /* The caller gave up, the answer is discarded. */
                return;
            }

            pending = it->second;
            pending_requests.erase(it);
        }

        try
        {
            pending->promise.set_value(parsed->result);
        }
        catch (const std::future_error&)
        {
            /* The caller gave up between the lookup and the delivery. */
        }
    }

    /**
     * @brief Answer one request with a JSON-RPC error this session produced.
     * @param[in] id Id of the request, null when it cannot be read.
     * @param[in] code Error code of the specification.
     * @param[in] text Error message.
     */
    void AnswerError(const nlohmann::json& id, int code, const std::string& text) noexcept
    {
        nlohmann::json err;
        err["code"] = code;
        err["message"] = text;

        nlohmann::json rsp;
        rsp["jsonrpc"] = "2.0";
        rsp["error"] = err;
        rsp["id"] = id;

        /* The error answer of this end is small, so the outcome is not reported. */
        (void)QueueWrite(std::make_shared<std::string>(rsp.dump()));
    }

    /**
     * @brief Queue one payload for the peer, the frame header is prepended.
     *
     * The payload is written as it is, so a caller which sends a request of its
     * own keeps its table entry: the session only frames and queues. The call
     * does not write anything itself, it signals the thread of the session,
     * which is why it may be made from any thread without blocking it.
     *
     * @param[in] data Payload to write.
     * @return Success, or the code of a payload the header cannot describe. A
     *         payload which cannot be framed is dropped, the peer gets no answer.
     */
    tl::expected<void, ErrorCode> QueueWrite(MsgPtr data) noexcept
    {
        const auto header = MakeFrameHeader(data->size());
        if (!header.has_value())
        {
            return tl::unexpected<ErrorCode>(header.error());
        }

        {
            std::lock_guard<std::mutex> lock(write_mutex);
            send_queue.push_back(std::make_shared<std::string>(*header));
            send_queue.push_back(std::move(data));
        }

        if (write_queued_event != nullptr)
        {
            ::SetEvent(write_queued_event);
        }

        return {};
    }

    /**
     * @brief Issue the write of the message at the front of the queue.
     *
     * A message is only issued when no other write is in flight, so the
     * `OVERLAPPED` of the write is never reused before its completion. The call
     * runs on the thread of the session.
     */
    void StartNextWrite()
    {
        MsgPtr message;
        {
            std::lock_guard<std::mutex> lock(write_mutex);
            if (in_write || send_queue.empty())
            {
                return;
            }

            message = send_queue.front();
            in_write = true;
        }

        const HANDLE pipe = Handle();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            /* Nothing can be written any more: the queue is released instead of hanging. */
            CompleteWrite();
            return;
        }

        std::memset(&write_ov, 0, sizeof(write_ov));
        write_ov.hEvent = write_event;

        DWORD transferred = 0;
        if (::WriteFile(pipe, message->data(), static_cast<DWORD>(message->size()), &transferred, &write_ov))
        {
            /* The write completed at once, its event reports the completion. */
            return;
        }

        if (::GetLastError() == ERROR_IO_PENDING)
        {
            return;
        }

        /* The write failed, the message is released and the queue moves on. */
        CompleteWrite();
    }

    /**
     * @brief Consume one completed write and issue the next queued message.
     * @return true when the session keeps serving, false when the connection
     *         ended.
     */
    bool OnWriteCompleted()
    {
        const HANDLE pipe = Handle();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            CompleteWrite();
            return false;
        }

        DWORD transferred = 0;
        if (!::GetOverlappedResult(pipe, &write_ov, &transferred, FALSE))
        {
            /* The write of the connection failed, the session keeps serving the read side. */
            CompleteWrite();
            return true;
        }

        CompleteWrite();
        StartNextWrite();
        return true;
    }

    /**
     * @brief Release the message at the front of the queue after a write.
     *
     * The wait for the pending writes is woken up, so an owner which drains the
     * session before it closes the connection learns that the message left the
     * queue, whatever the outcome of the write was.
     */
    void CompleteWrite()
    {
        {
            std::lock_guard<std::mutex> lock(write_mutex);
            in_write = false;
            if (!send_queue.empty())
            {
                send_queue.pop_front();
            }
        }

        write_cv.notify_all();
    }

    /**
     * @brief Queue one request and return the future of its answer.
     *
     * @param[in] method Method name.
     * @param[in] params Parameters of the method.
     * @param[out] out_id Id which identifies the request, zero when it was not
     *                    queued.
     * @return Future of the answer.
     */
    std::future<RemoteResult> SendRequest(const std::string& method, const nlohmann::json& params,
                                          std::uint64_t& out_id)
    {
        out_id = 0;

        if (ended.load() || Handle() == INVALID_HANDLE_VALUE)
        {
            /* Fail fast instead of keeping a caller which can never be answered. */
            std::promise<RemoteResult> promise;
            promise.set_value(tl::unexpected<RemoteError>(
                RemoteError{ kSessionClosedErrorCode, "the session is closed", nlohmann::json() }));
            return promise.get_future();
        }

        const std::uint64_t id = call_id_gen++;
        out_id = id;

        auto pending = std::make_shared<PendingRequest>(id);
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            pending_requests.emplace(id, pending);
        }

        nlohmann::json request;
        request["jsonrpc"] = "2.0";
        request["method"] = method;
        request["id"] = id;
        request["params"] = params;

        auto future = pending->promise.get_future();

        const auto queued = QueueWrite(std::make_shared<std::string>(request.dump()));
        if (!queued.has_value())
        {
            /*
             * A request the header cannot describe is never written, so the
             * caller is released at once instead of waiting for an answer which
             * cannot come.
             */
            Forget(id);
            pending->promise.set_value(tl::unexpected<RemoteError>(
                RemoteError{ kResponseDecodeErrorCode, "the request is too large", nlohmann::json() }));
        }

        return future;
    }

    /**
     * @brief Drop a request whose answer is not awaited any more.
     * @param[in] id Id of the request.
     */
    void Forget(std::uint64_t id)
    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending_requests.erase(id);
    }

    /**
     * @brief Answer every pending request with the error which ended the link.
     * @param[in] code Code of the failure.
     * @param[in] message Description of the failure.
     */
    void FailPending(int code, const std::string& message)
    {
        PendingRequestMap pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            pending.swap(pending_requests);
        }

        for (auto& item : pending)
        {
            try
            {
                item.second->promise.set_value(
                    tl::unexpected<RemoteError>(RemoteError{ code, message, nlohmann::json() }));
            }
            catch (const std::future_error&)
            {
                /* The caller gave up before the connection ended. */
            }
        }
    }

    /**
     * @brief Answer one request of the peer, or fail it when it is unknown.
     *
     * The entry of the request is released before the answer is queued, so an
     * answer which is produced twice reports the second call instead of sending
     * the request id twice.
     *
     * @param[in] request_id Id of the request, as its handler received it.
     * @param[in] result Result of the method or the error it reports.
     * @return Success, or the code of an answer the frame header cannot
     *         describe (`kRpcFrameTooLarge`).
     */
    tl::expected<void, ErrorCode> SendResponse(std::uint64_t request_id, const RemoteResult& result) noexcept
    {
        IncomingRequest::Ptr request;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            const auto                  it = incoming_requests.find(request_id);
            if (it != incoming_requests.end())
            {
                request = it->second;
                incoming_requests.erase(it);
            }
        }

        tl::expected<void, ErrorCode> outcome;

        if (request)
        {
            nlohmann::json rsp;
            rsp["jsonrpc"] = "2.0";

            if (result.has_value())
            {
                rsp["result"] = result.value();
            }
            else
            {
                const RemoteError& err = result.error();

                nlohmann::json j_err;
                j_err["code"] = err.code;
                j_err["message"] = err.message;
                if (!err.data.is_null())
                {
                    j_err["data"] = err.data;
                }

                rsp["error"] = j_err;
            }

            rsp["id"] = request->id;
            outcome = QueueWrite(std::make_shared<std::string>(rsp.dump()));
        }

        /* An answer of a request which is not in the table is dropped. */
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            if (inflight > 0)
            {
                --inflight;
            }
        }
        inflight_cv.notify_all();

        return outcome;
    }

    /**
     * @brief Close the pipe of the session.
     *
     * The pending read and write are cancelled, so their completions report the
     * end of the connection, and the thread of the session is told to leave its
     * loop. The handle itself is closed by that thread, because it is the only
     * one which may still hold a completion of it.
     */
    void Close()
    {
        const HANDLE pipe = Handle();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        ::CancelIoEx(pipe, nullptr);

        if (stop_event != nullptr)
        {
            ::SetEvent(stop_event);
        }

        /*
         * A session which never started has no thread which could close the
         * handle, so it is released here: the peer learns that the connection is
         * gone exactly like it does for a session which ran.
         */
        if (!started.load())
        {
            CloseHandleNow();
        }
    }

    /**
     * @brief Wait until the queued messages are written.
     * @param[in] timeout_ms Upper bound of the wait.
     * @return true when nothing is pending any more, otherwise false.
     */
    bool WaitForPendingWrites(std::uint32_t timeout_ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        std::unique_lock<std::mutex> lock(write_mutex);
        while (in_write || !send_queue.empty())
        {
            if (write_cv.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                return !in_write && send_queue.empty();
            }
        }

        return true;
    }

    /**
     * @brief Wait until every request this session received is answered.
     * @param[in] timeout_ms Upper bound of the wait.
     * @return true when nothing is pending any more, otherwise false.
     */
    bool WaitForIncomingResponses(std::uint32_t timeout_ms)
    {
        std::unique_lock<std::mutex> lock(request_mutex);
        return inflight_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return inflight == 0; });
    }

    /**
     * @brief Native handle of the pipe, for the log.
     * @return The handle, `INVALID_HANDLE_VALUE` when the session has none.
     */
    void* NativeHandle() const
    {
        const HANDLE pipe = Handle();
        return pipe == INVALID_HANDLE_VALUE ? nullptr : static_cast<void*>(pipe);
    }

    /**
     * @brief The pipe handle the session serves.
     * @return The handle, `INVALID_HANDLE_VALUE` when it was closed.
     */
    HANDLE Handle() const
    {
        return pipe_handle.load();
    }

    /**
     * @brief Close the pipe handle of the session.
     *
     * The handle is exchanged before it is closed, so two paths which end the
     * session never close it twice.
     */
    void CloseHandleNow()
    {
        HANDLE handle = pipe_handle.exchange(INVALID_HANDLE_VALUE);
        detail::CloseHandleIfValid(handle);
    }

    /**
     * @brief Start the thread which serves the connection.
     * @return Success, or the code of the reason the session cannot serve.
     */
    tl::expected<void, ErrorCode> Start() noexcept
    {
        if (started.exchange(true))
        {
            /* The session serves already, a second start keeps the thread it has. */
            return {};
        }

        if (!HasEvents())
        {
            /* Without its events the session serves nothing, which ends it like a closed link. */
            started = false;
            CloseHandleNow();
            return tl::unexpected<ErrorCode>(ErrorCode::kRemoteEventCreateFailed);
        }

        try
        {
            /*
             * The thread holds the state of the session, so a session which the
             * last caller releases while its thread still runs is not destroyed
             * under that thread.
             */
            auto self = shared_from_this();
            thread = std::thread([self]() { self->Run(); });
        }
        catch (const std::exception&)
        {
            started = false;
            CloseHandleNow();
            return tl::unexpected<ErrorCode>(ErrorCode::kRemoteThreadStartFailed);
        }

        return {};
    }

    /**
     * @brief End the session and fail the requests which still wait.
     */
    void Stop()
    {
        if (!ended.exchange(true))
        {
            FailPending(ERROR_OPERATION_ABORTED, "the session was stopped");
        }

        Close();

        if (thread.joinable() && std::this_thread::get_id() != thread.get_id())
        {
            thread.join();
            /* The thread closed the handle when it left its loop, this is a guard. */
            CloseHandleNow();
        }
        else
        {
            /*
             * The thread is detached when the caller runs on it: it closes the
             * handle when it leaves its loop. A session which never started has
             * no thread which could do that, so it releases the handle here.
             */
            detail::JoinOrDetach(thread);
            if (!started.load())
            {
                CloseHandleNow();
            }
        }

        NotifyClosed();
    }

    /* Number of the events the thread of the session waits on. */
    static constexpr DWORD kWaitCount = 4;

    /* Size of the buffer one overlapped read fills, in bytes. */
    static constexpr std::size_t kReadBufferSize = 4096;

    std::atomic<HANDLE> pipe_handle{ INVALID_HANDLE_VALUE }; /* Named pipe, owned by the session */
    DWORD               peer_pid = 0;                        /* Process at the other end */

    HANDLE read_event = nullptr;         /* Signalled when the pending read completed */
    HANDLE write_event = nullptr;        /* Signalled when the in-flight write completed */
    HANDLE write_queued_event = nullptr; /* Set by a caller which queued a message */
    HANDLE stop_event = nullptr;         /* Set by Close() and Stop() */

    OVERLAPPED read_ov = {};  /* Overlapped state of the read in flight */
    OVERLAPPED write_ov = {}; /* Overlapped state of the write in flight */

    std::thread      thread;           /* Thread which serves the connection */
    std::atomic_bool started{ false }; /* The thread was started */
    std::atomic_bool ended{ false };   /* The connection ended */

    std::size_t max_request_size = kMaxRpcPayloadSize; /* Largest request which is answered */

    std::list<MsgPtr>                    send_queue;       /* Send queue */
    std::vector<uint8_t>                 recv_data;        /* Recv data */
    std::array<uint8_t, kReadBufferSize> recv_buff = {};   /* Recv cache */
    bool                                 in_write = false; /* During write */
    std::mutex                           write_mutex;      /* Send queue and write state lock */
    std::condition_variable              write_cv;         /* Signals a finished write */

    MethodHandlerMap method_handlers; /* Registered methods */
    std::mutex       method_mutex;    /* Method table lock */

    ClosedHandler closed_handler; /* Handler which reports the end of the connection */
    std::mutex    callback_mutex; /* Closed handler lock */

    PendingRequestMap    pending_requests; /* Requests of this session which wait for an answer */
    std::mutex           pending_mutex;    /* Pending request table lock */
    std::atomic_uint64_t call_id_gen{ 0 }; /* Id generator of the requests of this session */

    IncomingRequestMap      incoming_requests; /* Requests of the peer which wait for an answer */
    std::mutex              request_mutex;     /* Incoming request table and in-flight lock */
    std::condition_variable inflight_cv;       /* Signals an answered request */
    std::size_t             inflight = 0;      /* Requests which are not answered */
    std::atomic_uint64_t    uid_gen{ 0 };      /* Id generator of the incoming requests */
};

/* Definition of the contract on the declaration. */
inline RemoteSession::RemoteSession() : data_(std::make_shared<Data>())
{
}

/* Definition of the contract on the declaration. */
inline RemoteSession::~RemoteSession()
{
    if (data_)
    {
        data_->Stop();
    }
    data_.reset();
}

/* Definition of the contract on the declaration. */
inline tl::expected<RemoteSession::Ptr, ErrorCode> RemoteSession::Create(HANDLE pipe, DWORD peer_pid) noexcept
{
    Ptr session(new RemoteSession);

    if (!session->data_->HasEvents())
    {
        /*
         * A session which cannot wait on its pipe serves nothing, so it closes
         * the handle it was given instead of leaving a connection which no
         * thread completes.
         */
        detail::CloseHandleIfValid(pipe);
        return tl::unexpected<ErrorCode>(ErrorCode::kRemoteEventCreateFailed);
    }

    session->data_->pipe_handle.store(pipe);
    session->data_->peer_pid = peer_pid;

    return session;
}

/* Definition of the contract on the declaration. */
inline void RemoteSession::SetMaxRequestSize(std::size_t max_request_size) noexcept
{
    data_->max_request_size = max_request_size;
}

/* Definition of the contract on the declaration. */
inline void RemoteSession::SetClosedHandler(ClosedHandler handler) noexcept
{
    std::lock_guard<std::mutex> lock(data_->callback_mutex);
    data_->closed_handler = std::move(handler);
}

/* Definition of the contract on the declaration. */
inline void RemoteSession::RegisterMethod(const std::string& method, MethodHandler handler) noexcept
{
    std::lock_guard<std::mutex> lock(data_->method_mutex);
    data_->method_handlers[method] = std::move(handler);
}

/* Definition of the contract on the declaration. */
inline tl::expected<void, ErrorCode> RemoteSession::SendResponse(std::uint64_t       request_id,
                                                                 const RemoteResult& result) noexcept
{
    return data_->SendResponse(request_id, result);
}

/* Definition of the contract on the declaration. */
inline std::future<RemoteResult> RemoteSession::Call(const std::string& method, const nlohmann::json& params) noexcept
{
    std::uint64_t id = 0;
    return data_->SendRequest(method, params, id);
}

/* Definition of the contract on the declaration. */
inline RemoteResult RemoteSession::Call(const std::string& method, const nlohmann::json& params,
                                        std::uint32_t timeout_ms) noexcept
{
    std::uint64_t id = 0;
    auto          future = data_->SendRequest(method, params, id);

    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready)
    {
        /*
         * The request is dropped, so an answer which arrives later is discarded
         * by OnAnswer() instead of being delivered to a caller which gave up.
         */
        data_->Forget(id);

        return tl::unexpected<RemoteError>(
            RemoteError{ static_cast<int>(ERROR_TIMEOUT), "the peer did not answer in time", nlohmann::json() });
    }

    return future.get();
}

/* Definition of the contract on the declaration. */
inline DWORD RemoteSession::PeerProcessId() const noexcept
{
    return data_->peer_pid;
}

/* Definition of the contract on the declaration. */
inline DWORD RemoteSession::PeerProcessIdOfRequest(std::uint64_t request_id) const noexcept
{
    {
        std::lock_guard<std::mutex> lock(data_->request_mutex);
        if (data_->incoming_requests.find(request_id) == data_->incoming_requests.end())
        {
            return 0;
        }
    }

    return data_->peer_pid;
}

/* Definition of the contract on the declaration. */
inline tl::expected<void, ErrorCode> RemoteSession::Start() noexcept
{
    return data_->Start();
}

/* Definition of the contract on the declaration. */
inline tl::expected<void, ErrorCode> RemoteSession::Send(MsgPtr data) noexcept
{
    return data_->QueueWrite(std::move(data));
}

/* Definition of the contract on the declaration. */
inline void RemoteSession::Close() noexcept
{
    data_->Close();
}

/* Definition of the contract on the declaration. */
inline bool RemoteSession::WaitForPendingWrites(std::uint32_t timeout_ms) noexcept
{
    return data_->WaitForPendingWrites(timeout_ms);
}

/* Definition of the contract on the declaration. */
inline bool RemoteSession::WaitForIncomingResponses(std::uint32_t timeout_ms) noexcept
{
    return data_->WaitForIncomingResponses(timeout_ms);
}

/* Definition of the contract on the declaration. */
inline void RemoteSession::Stop() noexcept
{
    if (data_)
    {
        data_->Stop();
    }
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_REMOTE_SESSION_DETAIL_HPP
