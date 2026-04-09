#ifndef APPBOX_COMMON_REMOTE_CLIENT_HPP
#define APPBOX_COMMON_REMOTE_CLIENT_HPP

#include <string>
#include <memory>
#include <future>
#include <nlohmann/json.hpp>
#include "RemoteServer.hpp"

namespace appbox
{

typedef std::future<RemoteResult> PipeResultFuture;

class RemoteClient
{
public:
    typedef std::shared_ptr<RemoteClient> Ptr;

    virtual ~RemoteClient();

    static Ptr Create(const std::string& pipe_path);
    bool       Start();

    /**
     * @brief Call a method on the pipe server
     * @param[in] method Method name to call
     * @param[in] param Parameters for the method
     * @return Response from the pipe server
     */
    PipeResultFuture Call(const std::string& method, const nlohmann::json& param);

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

        /*  RPC call*/
        auto future = Call(T::method, param);

        /* Wrap */
        return std::async(
            std::launch::deferred,
            [fut = std::move(future)]() mutable -> tl::expected<typename T::Rsp, RemoteError> {
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
                        RemoteError{ -1, std::string("Failed to deserialize response: ") + e.what(),
                                     result.value() });
                }
            });
    }

    struct Data;
    std::shared_ptr<Data> data_;

private:
    RemoteClient();
};

} // namespace appbox

#endif // APPBOX_COMMON_REMOTE_CLIENT_HPP
