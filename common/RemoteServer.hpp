#ifndef APPBOX_PIPE_SERVER_HPP
#define APPBOX_PIPE_SERVER_HPP

#include <tl/expected.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>

namespace appbox
{

struct RemoteError
{
    int            code;    /* Error code. */
    std::string    message; /* Error message. */
    nlohmann::json data;    /* Error data. */
};
typedef tl::expected<nlohmann::json, RemoteError> RemoteResult;

class RemoteServer : public std::enable_shared_from_this<RemoteServer>
{
public:
    typedef std::function<void(uint64_t, const nlohmann::json&)> MethodCallback;
    typedef std::shared_ptr<RemoteServer>                        Ptr;
    typedef std::weak_ptr<RemoteServer>                          WeakPtr;

    static Ptr Create(const std::string& pipe);
    virtual ~RemoteServer();

    /**
     * @brief Register a method callback for handling JSON requests
     * @param[in] method Method name to register
     * @param[in] callback Callback function to handle the method
     */
    void RegisterMethod(const std::string& method, MethodCallback callback);

    /**
     * @brief Start the pipe server and begin listening for incoming connections.
     */
    void Start();

    /**
     * @brief Send a JSON response to a client
     * @param[in] id Request ID
     * @param[in] result JSON response data
     */
    void SendResponse(uint64_t id, const RemoteResult& result);

    struct Data;
    typedef std::shared_ptr<Data> DataPtr;

    DataPtr data_;

private:
    RemoteServer();
};

} // namespace appbox

#endif // APPBOX_PIPE_SERVER_HPP
