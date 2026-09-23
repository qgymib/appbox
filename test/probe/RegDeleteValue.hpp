#ifndef APPBOX_TEST_PROBE_REGDELETEVALUE_HPP
#define APPBOX_TEST_PROBE_REGDELETEVALUE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Protocol of the delete value probe.
 *
 * The probe deletes a value of the sandbox view, reads it back and enumerates
 * the values which the view shows afterwards.
 */
struct ProtocolRegDeleteValue
{
    struct Req
    {
        std::string Root;  /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Key;   /* Key path relative to the root key. Encoding in UTF-8. */
        std::string Value; /* Name of the value to delete. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Value)
    };

    struct Rsp
    {
        DWORD open_code = static_cast<DWORD>(-1);     /* Open of the key with write access. */
        DWORD delete_code = static_cast<DWORD>(-1);   /* RegDeleteValueW() error code. */
        DWORD query_code = static_cast<DWORD>(-1);    /* Read of the value after the delete. */
        DWORD enum_code = static_cast<DWORD>(-1);     /* Enumeration of the values after the delete. */
        std::vector<std::string> names;               /* Value names the view shows, UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, delete_code, query_code, enum_code, names)
    };
};

/**
 * @brief Delete value probe: delete a value of the view and report the view
 *        after the call.
 *
 * The key is opened with write access, which the isolation copies up into the
 * hive for `WriteCopy`, so the delete runs on a hive handle. A value which
 * only the host registry holds has to be recorded as deleted (a whiteout):
 * neither the read through of the value nor the merged value enumeration may
 * show it afterwards, while the real registry keeps its value.
 */
extern Probe ProbeRegDeleteValue;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGDELETEVALUE_HPP
