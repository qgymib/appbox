#ifndef APPBOX_TEST_PROBE_REGRENAMEKEY_HPP
#define APPBOX_TEST_PROBE_REGRENAMEKEY_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Protocol of the rename key probe.
 *
 * The probe renames a key of the sandbox view and reports what the view shows
 * afterwards, so a test can compare it with the state of the real registry.
 */
struct ProtocolRegRenameKey
{
    struct Req
    {
        std::string Root;    /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Key;     /* Key path relative to the root key. Encoding in UTF-8. */
        std::string NewName; /* New name of the key, a single component. Encoding in UTF-8. */
        std::string Mode;    /* `hive_handle` opens the key for writing, which the isolation answers with a
                              * handle of the sandbox hive; `read_handle` opens it read only, which the
                              * isolation answers with a handle of the host layer (the read through). */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, NewName, Mode)
    };

    struct Rsp
    {
        DWORD open_code = static_cast<DWORD>(-1);          /* Open of the handle the rename runs on. */
        DWORD rename_code = static_cast<DWORD>(-1);        /* The rename call itself. */
        DWORD old_open_code = static_cast<DWORD>(-1);      /* Read access open of the old name. */
        std::map<std::string, std::string> old_values;     /* String values of the old name. */
        DWORD new_open_code = static_cast<DWORD>(-1);      /* Read access open of the new name. */
        std::map<std::string, std::string> new_values;     /* String values of the new name. */
        std::vector<std::string>           parent_subkeys; /* Sub key names the parent reports in the view. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, open_code, rename_code, old_open_code, old_values,
                                                    new_open_code, new_values, parent_subkeys)
    };
};

/**
 * @brief Rename key probe: rename a key of the view and report the view after
 *        the call.
 *
 * The `hive_handle` mode opens the key with a write access, which the isolation
 * copies up into the hive, and the `read_handle` mode opens it read only, which
 * the read through answers with a handle of the real registry. The rename runs
 * through `NtRenameKey`, which has no Win32 wrapper.
 *
 * The response reports the string values of the old name and of the new name as
 * the view shows them, plus the sub key names of the parent key, so a test can
 * pin which of the two names the merged view holds.
 */
extern Probe ProbeRegRenameKey;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGRENAMEKEY_HPP
