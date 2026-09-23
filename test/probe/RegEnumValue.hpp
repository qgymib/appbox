#ifndef APPBOX_TEST_PROBE_REGENUMVALUE_HPP
#define APPBOX_TEST_PROBE_REGENUMVALUE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace appbox::test
{

struct ProtocolRegEnumValue
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
        DWORD value_count = 0;                     /* Value count reported by RegQueryInfoKeyW(). */
        DWORD enum_code = static_cast<DWORD>(-1);  /* Last RegEnumValueW() error code. */
        std::vector<std::string> names;            /* Value names in enumeration order. UTF-8. */
        std::map<std::string, std::string> values; /* Value name to REG_SZ data. UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, count_code, value_count, enum_code, names, values)
    };
};

/**
 * @brief Registry value enumeration probe: open the key below HKCU, enumerate
 *        every value name and read the REG_SZ data of each value back.
 */
extern Probe ProbeRegEnumValue;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGENUMVALUE_HPP
