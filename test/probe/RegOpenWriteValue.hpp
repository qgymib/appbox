#ifndef APPBOX_TEST_PROBE_REGOPENWRITEVALUE_HPP
#define APPBOX_TEST_PROBE_REGOPENWRITEVALUE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

struct ProtocolRegOpenWriteValue
{

    struct Req
    {
        std::string Root;  /* Root key name, for example HKEY_LOCAL_MACHINE. Empty means HKEY_CURRENT_USER. */
        std::string Key;   /* Key path relative to the root key. Encoding in UTF-8. */
        std::string Value; /* Value name. Encoding in UTF-8. */
        std::string Data;  /* Text of the REG_SZ value to write. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Value, Data)
    };

    struct Rsp
    {
        DWORD open_code = static_cast<DWORD>(-1);  /* RegOpenKeyExW() error code. */
        DWORD set_code = static_cast<DWORD>(-1);   /* RegSetValueExW() error code. */
        DWORD query_code = static_cast<DWORD>(-1); /* RegQueryValueExW() error code. */
        DWORD type = 0;                            /* Value type. */
        std::string readback;                      /* Value data which was read back. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, set_code, query_code, type, readback)
    };
};

/**
 * @brief Open write probe: open an existing key with write access, write a
 *        `REG_SZ` value and read it back.
 *
 * The key is opened instead of created, so the isolation mode of the key
 * decides which layer answers the call. This is the case the copy-up of the
 * `WriteCopy` mode has to catch: a key which only the host registry holds must
 * be opened inside the sandbox hive, so the value never reaches the host
 * registry. A key which neither layer holds still reports that it does not
 * exist, because an open never creates a key.
 */
extern Probe ProbeRegOpenWriteValue;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGOPENWRITEVALUE_HPP
