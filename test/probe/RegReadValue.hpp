#ifndef APPBOX_TEST_PROBE_REGREADVALUE_HPP
#define APPBOX_TEST_PROBE_REGREADVALUE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolRegReadValue
{

    struct Req
    {
        std::string Root;  /* Root key name, for example HKEY_LOCAL_MACHINE. Empty means HKEY_CURRENT_USER. */
        std::string Key;   /* Key path relative to the root key. Encoding in UTF-8. */
        std::string Value; /* Value name. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Value)
    };

    struct Rsp
    {
        DWORD open_code = static_cast<DWORD>(-1);   /* RegOpenKeyExW() error code. */
        DWORD query_code = static_cast<DWORD>(-1);  /* RegQueryValueExW() error code. */
        DWORD type = 0;                             /* Value type. */
        std::string data;                           /* Value data. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, query_code, type, data)
    };
};

/**
 * @brief Registry read probe: open an existing key and read a value.
 */
extern Probe ProbeRegReadValue;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGREADVALUE_HPP
