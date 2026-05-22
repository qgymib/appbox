#ifndef APPBOX_UTILS_PIPECLIENT_HPP
#define APPBOX_UTILS_PIPECLIENT_HPP

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace appbox
{

struct PipeClient
{
    /**
     * @brief Create a pipe client.
     * @param[in] path Pipe path.
     */
    PipeClient(const std::string& path);
    ~PipeClient();

    /**
     * @brief Start the pipe client.
     * @return Start result.
     */
    bool Start();

    /**
     * @brief Call a method.
     * @param[in] method Method name.
     * @param[in] params Method parameters.
     * @param[out] rsp Method result.
     * @return Call result.
     */
    bool Call(const std::string& method, const nlohmann::json& params, nlohmann::json& rsp);

    struct Data;
    Data* data_;
};

} // namespace appbox

#endif
