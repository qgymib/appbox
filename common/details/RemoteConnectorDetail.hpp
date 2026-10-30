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
    Data() : io_context(std::make_shared<asio::io_context>())
    {
    }

    ~Data()
    {
        Stop();
    }

    /**
     * @brief End the IO thread and release the session which runs on it.
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

        /*
         * The session goes before the context: its handle refers to the context
         * the IO thread of the connector ran on.
         */
        session.reset();
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
     * calls Connect(). A pipe which does not exist yet or which is busy is
     * retried with a growing delay until the budget is used up.
     *
     * @param[in] timeout_ms Budget of the connection attempts.
     * @return The connected session, null when the pipe cannot be opened.
     */
    RemoteSession::Ptr Connect(std::uint32_t timeout_ms)
    {
        if (session != nullptr)
        {
            SPDLOG_WARN("the connector is connected already");
            return session;
        }

        if (!io_context)
        {
            SPDLOG_ERROR("the connector is stopped");
            return nullptr;
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
                SPDLOG_ERROR(L"cannot open pipe: {}({})", GetErrorString(err), err);
                return nullptr;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                SPDLOG_ERROR("cannot open pipe: the budget of {} ms is used up", timeout_ms);
                return nullptr;
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
            return nullptr;
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
            return nullptr;
        }

        session = RemoteSession::Create(handle, server_pid, io_context);
        session->Start();

        /*
         * The thread does not log when it ends: the connector may be destroyed
         * by a static destructor of the process, which runs after the logger of
         * the process is gone.
         */
        auto context = io_context;
        thread = std::thread([context]() { context->run(); });

        return session;
    }

    /* Number of the first delay between two connection attempts, in milliseconds. */
    static constexpr std::uint32_t kFirstRetryDelayMs = 20;

    /* Upper bound of the delay between two connection attempts, in milliseconds. */
    static constexpr std::uint32_t kMaxRetryDelayMs = 200;

    std::string                       pipe_path;      /* Named pipe path */
    std::shared_ptr<asio::io_context> io_context;     /* IO context of the IO thread */
    std::thread                       thread;         /* IO thread */
    RemoteSession::Ptr                session;        /* Session of the connection */
    DWORD                             server_pid = 0; /* Process of the server */
};

/* Definition of the contract on the declaration. */
inline RemoteConnector::RemoteConnector() : data_(std::make_shared<Data>())
{
}

/* Definition of the contract on the declaration. */
inline RemoteConnector::~RemoteConnector()
{
    if (data_)
    {
        data_->Stop();
    }
    data_.reset();
}

/* Definition of the contract on the declaration. */
inline RemoteConnector::Ptr RemoteConnector::Create(const std::string& pipe_path)
{
    Ptr obj(new RemoteConnector);
    obj->data_->pipe_path = pipe_path;
    return obj;
}

/* Definition of the contract on the declaration. */
inline RemoteSession::Ptr RemoteConnector::Connect(std::uint32_t timeout_ms)
{
    return data_->Connect(timeout_ms);
}

/* Definition of the contract on the declaration. */
inline DWORD RemoteConnector::ServerProcessId() const
{
    return data_->server_pid;
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_REMOTE_CONNECTOR_DETAIL_HPP
