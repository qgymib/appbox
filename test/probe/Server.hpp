#ifndef APPBOX_TEST_PROBE_SERVER_HPP
#define APPBOX_TEST_PROBE_SERVER_HPP

#include <string>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProbeRequest
{
    static constexpr char* Method = "ProbeRequest";

    struct Req
    {
        std::string key;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, key)
    };

    struct Rsp
    {
        std::string    name;
        nlohmann::json data;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, name, data)
    };
};

struct ProbeResponse
{
    static constexpr char* Method = "ProbeResponse";

    struct Req
    {
        std::string    key;
        nlohmann::json result;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, key, result)
    };

    struct Rsp
    {
        int _;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, _)
    };
};

/**
 * @brief Call a probe function.
 * @param[in] name The name of the probe function, encoding in UTF-8.
 * @param[in] data The data to pass to the probe function.
 * @return The result of the probe function.
 */
nlohmann::json ProbeCall(const std::string& name, const nlohmann::json& data);

} // namespace appbox::test

#endif
