#ifndef APPBOX_COMMON_REMOTE_SESSION_HPP
#define APPBOX_COMMON_REMOTE_SESSION_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <windows.h>
#include <asio.hpp>
#include <memory>
#include <string>
#include <functional>

namespace appbox
{

struct RemoteProtocol
{
    uint32_t magic = 0x424F583A;
    uint32_t length = 0;
};

class RemoteSession : public std::enable_shared_from_this<RemoteSession>
{
public:
    typedef std::shared_ptr<RemoteSession>                         Ptr;
    typedef std::shared_ptr<std::string>                         MsgPtr;
    typedef std::function<void(const asio::error_code&, MsgPtr)> DataReceivedCallback;

    static Ptr Create(std::shared_ptr<asio::windows::stream_handle> pipe, DataReceivedCallback cb);
    ~RemoteSession();

    void Start();
    void Send(MsgPtr data);

    struct Data;
    std::shared_ptr<Data> data_;

private:
    RemoteSession();
};

} // namespace appbox

#endif // APPBOX_COMMON_REMOTE_SESSION_HPP
