#ifndef APPBOX_TEST_PROBE_BIND_HPP
#define APPBOX_TEST_PROBE_BIND_HPP

#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

/**
 * @brief Message which tells the test executable which handle owns a probe.
 *
 * The probe sends it as the first request of its connection, before it answers
 * anything: a launcher run starts one probe per startup file, and every process
 * of the run carries the same key, so the key alone does not tell the processes
 * apart. The message is the only request a probe issues; every other message of
 * the connection is a request of the test executable.
 */
struct ProtocolBind
{
    static constexpr const char* Method = "Bind";

    struct Req
    {
        std::string key; /* Key of the handle which started the process. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, key)
    };

    struct Rsp
    {
        int _ = 0; /* Useless field. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, _)
    };
};

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_BIND_HPP
