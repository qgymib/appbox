#ifndef APPBOX_TEST_PROBE_REGSHADOWREAD_HPP
#define APPBOX_TEST_PROBE_REGSHADOWREAD_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

struct ProtocolRegShadowRead
{

    struct Req
    {
        std::string Key;   /* Key path relative to HKCU. Encoding in UTF-8. */
        std::string Value; /* Value name. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Key, Value)
    };

    struct Rsp
    {
        DWORD create_code = static_cast<DWORD>(-1); /* RegCreateKeyExW() error code. */
        DWORD disposition = 0;                      /* REG_CREATED_NEW_KEY / REG_OPENED_EXISTING_KEY. */
        DWORD query_code = static_cast<DWORD>(-1);  /* RegQueryValueExW() error code. */
        DWORD type = 0;                             /* Value type. */
        std::string data;                           /* Value data which was read. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, create_code, disposition, query_code, type, data)
    };
};

/**
 * @brief Shadow key probe: create the key inside the sandbox (which shadows a
 *        key of the real registry) and read a value which only exists in the
 *        real registry through the shadow.
 */
extern Probe ProbeRegShadowRead;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGSHADOWREAD_HPP
