#ifndef APPBOX_COMMON_DETAILS_REMOTE_CONNECTOR_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REMOTE_CONNECTOR_DETAIL_HPP

/*
 * Implementation of `common/RemoteConnector.hpp`: the internal state of the
 * connector, the connection of the pipe and the members of the class. The header
 * is included by the public header and is not meant to be included on its own.
 */

#include "RemoteConnector.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
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
inline bool IsRetryableConnectorConnectError(DWORD error)
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
 * @brief Internal state of one RemoteConnector.
 */
struct RemoteConnector::Data
{
    Data() = default;

    ~Data()
    {
        /*
         * The session owns the handle and the thread which serves it, so
         * releasing it here closes the connection. A session which the caller
         * keeps alive is not affected.
         */
        session.reset();
    }

    /**
     * @brief Open the pipe and start the session.
     *
     * The connect is a short blocking operation, it runs on the thread which
     * calls Connect(). A pipe which does not exist yet or which is busy is
     * retried with a growing delay until the budget is used up.
     *
     * @param[in] timeout_ms Budget of the connection attempts.
     * @return The connected session, or the code of the reason it cannot be
     *         opened.
     */
    tl::expected<RemoteSession::Ptr, ErrorCode> Connect(std::uint32_t timeout_ms) noexcept
    {
        if (session != nullptr)
        {
            return session;
        }

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
            if (!detail::IsRetryableConnectorConnectError(err))
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kPipeConnectFailed);
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kPipeConnectTimeout);
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
            CloseHandle(pipe);
            return tl::unexpected<ErrorCode>(ErrorCode::kPipeStateFailed);
        }

        /*
         * A process which cannot be queried leaves the peer unknown, which the
         * caller reads as the zero it starts with.
         */
        ULONG server_process_id = 0;
        if (GetNamedPipeServerProcessId(pipe, &server_process_id))
        {
            server_pid = server_process_id;
        }

        /*
         * The session takes ownership of the handle and starts the thread which
         * serves it, so a caller may send a request as soon as the call returns.
         */
        const auto created = RemoteSession::Create(pipe, server_pid);
        if (!created.has_value())
        {
            /* The session closed the handle it was given. */
            return tl::unexpected<ErrorCode>(created.error());
        }

        const auto started = (*created)->Start();
        if (!started.has_value())
        {
            return tl::unexpected<ErrorCode>(started.error());
        }

        session = *created;
        return session;
    }

    /* Number of the first delay between two connection attempts, in milliseconds. */
    static constexpr std::uint32_t kFirstRetryDelayMs = 20;

    /* Upper bound of the delay between two connection attempts, in milliseconds. */
    static constexpr std::uint32_t kMaxRetryDelayMs = 200;

    std::string        pipe_path;      /* Named pipe path */
    RemoteSession::Ptr session;        /* Session of the connection */
    DWORD              server_pid = 0; /* Process of the server */
};

/* Definition of the contract on the declaration. */
inline RemoteConnector::RemoteConnector() : data_(std::make_shared<Data>())
{
}

/* Definition of the contract on the declaration. */
inline RemoteConnector::~RemoteConnector()
{
    data_.reset();
}

/* Definition of the contract on the declaration. */
inline tl::expected<RemoteConnector::Ptr, ErrorCode> RemoteConnector::Create(const std::string& pipe_path) noexcept
{
    Ptr obj(new RemoteConnector);
    obj->data_->pipe_path = pipe_path;
    return obj;
}

/* Definition of the contract on the declaration. */
inline tl::expected<RemoteSession::Ptr, ErrorCode> RemoteConnector::Connect(std::uint32_t timeout_ms) noexcept
{
    return data_->Connect(timeout_ms);
}

/* Definition of the contract on the declaration. */
inline DWORD RemoteConnector::ServerProcessId() const noexcept
{
    return data_->server_pid;
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_REMOTE_CONNECTOR_DETAIL_HPP
