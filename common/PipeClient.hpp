#ifndef APPBOX_PIPE_CLIENT_HPP
#define APPBOX_PIPE_CLIENT_HPP

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace appbox
{

struct PipeClientResponse
{
    typedef std::shared_ptr<PipeClientResponse> Ptr;
};

struct PipeClient
{
    explicit PipeClient(const std::wstring& pipe_path);
    virtual ~PipeClient();

    bool Start();

    /**
     * @brief Call a method on the pipe server
     * @param[in] method Method name to call
     * @param[in] param Parameters for the method
     * @return Response from the pipe server
     */
    PipeClientResponse::Ptr Call(const std::string& method, const nlohmann::json& param);

    struct Data;
    std::shared_ptr<Data> data_;
};

} // namespace appbox

#endif // APPBOX_PIPE_CLIENT_HPP
