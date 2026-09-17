#ifndef APPBOX_TEST_PROBE_REGQUERYKEYNAME_HPP
#define APPBOX_TEST_PROBE_REGQUERYKEYNAME_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

struct ProtocolRegQueryKeyName
{

    struct Req
    {
        std::string Key; /* Key path relative to HKCU. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Key)
    };

    struct Rsp
    {
        DWORD create_code = static_cast<DWORD>(-1); /* RegCreateKeyExW() error code. */
        DWORD disposition = 0;                      /* REG_CREATED_NEW_KEY / REG_OPENED_EXISTING_KEY. */
        DWORD query_key_code = static_cast<DWORD>(-1);   /* NTSTATUS of NtQueryKey(). */
        DWORD query_object_code = static_cast<DWORD>(-1); /* NTSTATUS of NtQueryObject(). */
        std::string key_name;    /* KeyNameInformation of NtQueryKey(). UTF-8. */
        std::string object_name; /* ObjectNameInformation of NtQueryObject(). UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, create_code, disposition, query_key_code, query_object_code,
                                                    key_name, object_name)
    };
};

/**
 * @brief Key name probe: create a key below HKCU inside the sandbox and report
 *        the key names of NtQueryKey(KeyNameInformation) and
 *        NtQueryObject(ObjectNameInformation).
 */
extern Probe ProbeRegQueryKeyName;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGQUERYKEYNAME_HPP
