#ifndef APPBOX_TEST_PROBE_REGWRITEVALUE_HPP
#define APPBOX_TEST_PROBE_REGWRITEVALUE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolRegWriteValue
{

    struct Req
    {
        std::string Key;   /* Key path relative to HKCU. Encoding in UTF-8. */
        std::string Value; /* Value name. Encoding in UTF-8. */
        std::string Data;  /* Value data (REG_SZ). Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Key, Value, Data)
    };

    struct Rsp
    {
        DWORD create_code = static_cast<DWORD>(-1); /* RegCreateKeyExW() error code. */
        DWORD disposition = 0;                       /* REG_CREATED_NEW_KEY / REG_OPENED_EXISTING_KEY. */
        DWORD set_code = static_cast<DWORD>(-1);     /* RegSetValueExW() error code. */
        DWORD query_code = static_cast<DWORD>(-1);   /* RegQueryValueExW() error code. */
        DWORD type = 0;                              /* Value type. */
        std::string readback;                        /* Value data which was read back. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, create_code, disposition, set_code, query_code, type,
                                                    readback)
    };
};

/**
 * @brief Registry write probe: create the key, write a value and read it back.
 */
extern Probe ProbeRegWriteValue;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGWRITEVALUE_HPP
