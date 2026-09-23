#ifndef APPBOX_TEST_PROBE_REGQUERYMULTIPLEVALUES_HPP
#define APPBOX_TEST_PROBE_REGQUERYMULTIPLEVALUES_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Protocol of the multi value query probe.
 *
 * The probe queries several values of a key in one call and reports the type
 * and the data of every entry, so a test can verify that a batch which mixes
 * the two layers is answered correctly.
 */
struct ProtocolRegQueryMultipleValues
{
    struct Req
    {
        std::string              Root;  /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string              Key;   /* Key path relative to the root key. Encoding in UTF-8. */
        std::vector<std::string> Names; /* Names of the values to query. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Names)
    };

    struct Rsp
    {
        DWORD                    open_code = static_cast<DWORD>(-1);  /* Open of the key. */
        DWORD                    query_code = static_cast<DWORD>(-1); /* RegQueryMultipleValuesW(). */
        DWORD                    total_size = 0;                      /* Size the call reports. */
        std::vector<DWORD>       types;                               /* Value type of every entry. */
        std::vector<std::string> values;                              /* Data of every entry. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, query_code, total_size, types, values)
    };
};

/**
 * @brief Multi value query probe: query a batch of values of a key.
 *
 * `RegQueryMultipleValuesW` reaches `NtQueryMultipleValueKey`, which the
 * registry isolation answers from both layers: a value of the hive and a value
 * which only the host holds are part of the same batch, and a value which the
 * isolation hides (a mode or a whiteout) fails the batch the way the kernel
 * reports a missing value.
 *
 * The data of a `REG_SZ` value is reported as its text, every other type as a
 * hexadecimal byte dump.
 */
extern Probe ProbeRegQueryMultipleValues;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGQUERYMULTIPLEVALUES_HPP
