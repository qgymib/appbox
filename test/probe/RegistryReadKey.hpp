#ifndef APPBOX_TEST_PROBE_REGISTRY_READ_KEY_HPP
#define APPBOX_TEST_PROBE_REGISTRY_READ_KEY_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolRegistryReadKey
{
    struct Req
    {
        std::string path;
        std::string key;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, path, key)
    };

    struct Rsp
    {
        DWORD       code = 0;
        std::string value;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, value)
    };
};

/**
 * @brief RegistryReadKey probe.
 */
extern Probe ProbeRegistryReadKey;

}

#endif
