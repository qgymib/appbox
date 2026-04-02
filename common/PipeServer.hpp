#ifndef APPBOX_PIPE_SERVER_HPP
#define APPBOX_PIPE_SERVER_HPP

#include <functional>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace appbox
{

class PipeServer : public std::enable_shared_from_this<PipeServer>
{
public:
    typedef std::function<void(uint64_t, const nlohmann::json&)> MethodCallback;
    typedef std::shared_ptr<PipeServer>                Ptr;
    typedef std::weak_ptr<PipeServer>                  WeakPtr;

    static Ptr Create(const std::wstring& pipe);
    virtual ~PipeServer();

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

    struct Data;
    Data* data_;

private:
    PipeServer();
};

} // namespace appbox

#endif // APPBOX_PIPE_SERVER_HPP
