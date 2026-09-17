#ifndef APPBOX_TEST_PROBE_REGENUMKEY_HPP
#define APPBOX_TEST_PROBE_REGENUMKEY_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

struct ProtocolRegEnumKey
{

    struct Req
    {
        std::string Key; /* Key path relative to HKCU. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Key)
    };

    struct Rsp
    {
        DWORD open_code = static_cast<DWORD>(-1);  /* RegOpenKeyExW() error code. */
        DWORD count_code = static_cast<DWORD>(-1); /* RegQueryInfoKeyW() error code. */
        DWORD subkey_count = 0;                    /* Sub key count reported by RegQueryInfoKeyW(). */
        DWORD enum_code = static_cast<DWORD>(-1);  /* Last RegEnumKeyExW() error code. */
        std::vector<std::string> names;            /* Sub key names in enumeration order. UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, count_code, subkey_count, enum_code, names)
    };
};

/**
 * @brief Registry sub key enumeration probe: open the key below HKCU, report
 *        the sub key count and enumerate every sub key name.
 */
extern Probe ProbeRegEnumKey;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGENUMKEY_HPP
