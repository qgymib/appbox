#include "utils/Winsock.hpp" /* Must be first include file */
#include "Completion.hpp"
#include "Proxy.hpp"
#include "utils/Log.hpp"
#include "utils/ProxyHook.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

/**
 * @brief Class of the information which names the completion port of a handle.
 *
 * The class belongs to the interface of the object manager and not to a driver,
 * so it answers for a socket as well: it is how the sandbox learns the port an
 * application bound its socket to without hooking the binding.
 */
constexpr int kFileCompletionInformation = 30;

/** Largest number of the receives which wait for a datagram at once. */
constexpr std::size_t kMaxPendingReceives = 256;

/** Time a receive waits for a datagram before it gives up, in milliseconds. */
constexpr DWORD kReceiveTimeoutMs = 30000;

/** Signature of the call which reads the completion port of a handle. */
using QueryInformationFileFn = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);

/**
 * @brief Information the call which reads the completion port of a handle fills.
 */
struct CompletionInformation
{
    HANDLE    port; ///< Port the handle reports its completions to.
    ULONG_PTR key;  ///< Key the completions of the handle carry.
};

/**
 * @brief One datagram which waits for the worker of the sandbox.
 */
struct PendingReceive
{
    SOCKET              socket = INVALID_SOCKET; ///< Socket to receive from.
    std::vector<WSABUF> buffers;                 ///< Buffers of the application.
    bool                report_source = false;   ///< Whether the sender is reported.
    sockaddr*           from = nullptr;          ///< Buffer of the address of the sender.
    int*                fromlen = nullptr;       ///< Size of the buffer of the address.
    LPWSAOVERLAPPED     overlapped = nullptr;    ///< The operation of the application.
    int                 family = AF_UNSPEC;      ///< Family of the address the socket works with.
    DWORD               deadline = 0;            ///< Tick count the receive has to finish before.
};

/**
 * @brief Queue and worker of the receives which wait for a datagram.
 */
struct ReceiveQueue
{
    std::mutex                 mutex; ///< Protects the queue.
    std::condition_variable    ready; ///< Signals a datagram of the queue.
    std::deque<PendingReceive> items; ///< Receives which wait for a datagram.
};

/**
 * @brief The queue of the process.
 * @return The queue.
 */
ReceiveQueue& Queue()
{
    static ReceiveQueue queue;
    return queue;
}

/**
 * @brief Read the completion port of a handle.
 * @param[in] socket Handle to read.
 * @param[out] port Port of the handle, null when it has none.
 * @param[out] key Key of the completions of the handle.
 * @return true when the port was read.
 */
bool CompletionPortOf(SOCKET socket, HANDLE& port, ULONG_PTR& key)
{
    port = nullptr;
    key = 0;

    static QueryInformationFileFn query = []() -> QueryInformationFileFn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<QueryInformationFileFn>(GetProcAddress(ntdll, "NtQueryInformationFile"));
    }();

    if (query == nullptr)
    {
        return false;
    }

    CompletionInformation information = {};
    IO_STATUS_BLOCK       status = {};
    const NTSTATUS        result =
        query(reinterpret_cast<HANDLE>(socket), &status, &information, sizeof(information), kFileCompletionInformation);
    if (result < 0)
    {
        return false;
    }

    port = information.port;
    key = information.key;
    return true;
}

/**
 * @brief Serve the receives which wait for a datagram.
 *
 * The worker waits for a datagram of any socket of the queue, reads the one
 * which is ready, and reports its result to the application. A receive which
 * does not finish within its deadline is reported as a timeout, so a socket
 * which never answers does not keep an operation of the application forever.
 */
void RunWorker()
{
    for (;;)
    {
        std::vector<PendingReceive> batch;
        {
            std::unique_lock<std::mutex> lock(Queue().mutex);
            if (Queue().items.empty())
            {
                Queue().ready.wait_for(lock, std::chrono::milliseconds(20), []() { return !Queue().items.empty(); });
            }
            batch.assign(Queue().items.begin(), Queue().items.end());
            Queue().items.clear();
        }

        if (batch.empty())
        {
            continue;
        }

        /* One wait for every socket of the batch, so a datagram of any of them
         * is served by the pass which found it. */
        fd_set set;
        FD_ZERO(&set);
        for (const PendingReceive& job : batch)
        {
            FD_SET(job.socket, &set);
        }

        timeval   timeout = {};
        const int ready = ::select(0, &set, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
        {
            const int error = WSAGetLastError();
            for (const PendingReceive& job : batch)
            {
                appbox::network::CompleteOverlapped(job.socket, job.overlapped, 0, error);
            }
            continue;
        }

        appbox::network::Proxy* proxy = appbox::network::ProxyOfProcess();
        std::size_t             waiting = 0;
        for (const PendingReceive& job : batch)
        {
            if (proxy == nullptr)
            {
                appbox::network::CompleteOverlapped(job.socket, job.overlapped, 0, WSAENOTSOCK);
                continue;
            }

            if (FD_ISSET(job.socket, &set) == 0)
            {
                /* The datagram did not arrive yet, so the receive waits again. */
                if (GetTickCount() > job.deadline)
                {
                    appbox::network::CompleteOverlapped(job.socket, job.overlapped, 0, WSAETIMEDOUT);
                    continue;
                }
                std::lock_guard<std::mutex> lock(Queue().mutex);
                Queue().items.push_back(job);
                ++waiting;
                continue;
            }

            appbox::network::socks5::Endpoint source;
            const int                         received =
                proxy->ReceiveDatagram(job.socket, job.buffers.data(), job.buffers.size(), source, job.deadline);
            if (received == SOCKET_ERROR)
            {
                appbox::network::CompleteOverlapped(job.socket, job.overlapped, 0, WSAGetLastError());
                continue;
            }

            if (job.report_source && job.from != nullptr && job.fromlen != nullptr)
            {
                appbox::network::WriteAddress(source, job.family, job.from, job.fromlen);
            }
            appbox::network::CompleteOverlapped(job.socket, job.overlapped, static_cast<DWORD>(received), 0);
        }

        if (waiting != 0)
        {
            /* A receive which is not ready yet must not spin the worker. */
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

/**
 * @brief Start the worker of the sandbox once.
 */
void EnsureWorker()
{
    static std::once_flag started;
    std::call_once(started, []() {
        std::thread worker(RunWorker);
        worker.detach();
        LOG_D("the worker of the overlapped datagrams is running");
    });
}

} // namespace

void appbox::network::CompleteOverlapped(SOCKET socket, LPWSAOVERLAPPED overlapped, DWORD bytes, DWORD error)
{
    if (overlapped == nullptr)
    {
        return;
    }

    overlapped->Internal = error == 0 ? 0 : 0xC0000001; /* STATUS_UNSUCCESSFUL */
    overlapped->InternalHigh = bytes;

    /*
     * The event of an operation which is made with a completion port is not
     * used, and a caller marks that by setting the low bit of the handle.
     */
    const HANDLE event = overlapped->hEvent;
    if (event != nullptr && (reinterpret_cast<ULONG_PTR>(event) & 1) == 0)
    {
        SetEvent(event);
    }

    HANDLE    port = nullptr;
    ULONG_PTR key = 0;
    if (CompletionPortOf(socket, port, key) && port != nullptr)
    {
        if (!PostQueuedCompletionStatus(port, bytes, key, overlapped))
        {
            LOG_W("the completion of a datagram could not be queued: {}", GetLastError());
        }
    }
}

bool appbox::network::QueueDatagramReceive(SOCKET socket, const WSABUF* buffers, std::size_t count, bool report_source,
                                           sockaddr* from, int* fromlen, LPWSAOVERLAPPED overlapped, int family)
{
    if (buffers == nullptr || count == 0 || overlapped == nullptr)
    {
        return false;
    }

    EnsureWorker();

    PendingReceive job;
    job.socket = socket;
    job.buffers.assign(buffers, buffers + count);
    job.report_source = report_source;
    job.from = from;
    job.fromlen = fromlen;
    job.overlapped = overlapped;
    job.family = family;
    job.deadline = GetTickCount() + kReceiveTimeoutMs;

    {
        std::lock_guard<std::mutex> lock(Queue().mutex);
        if (Queue().items.size() >= kMaxPendingReceives)
        {
            return false;
        }
        Queue().items.push_back(job);
    }
    Queue().ready.notify_one();
    return true;
}
