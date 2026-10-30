#ifndef APPBOX_TEST_PROBE_REGRENAMEKEY_HPP
#define APPBOX_TEST_PROBE_REGRENAMEKEY_HPP

#include "WinAPI.h" /* Must be first include file */
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
    static constexpr const char* Method = "RegRenameKey";

    /**
     * @brief Layer of the handle the rename runs on.
     *
     * - `ReadHandle` - the key is opened read only, which the isolation answers
     *   with a handle of the host layer (the read through). This is the default.
     * - `HiveHandle` - the key is opened for writing, which the isolation
     *   answers with a handle of the sandbox hive.
     */
    enum class ModeKind
    {
        ReadHandle, ///< Read only open, answered with a handle of the host layer.
        HiveHandle  ///< Write open, answered with a handle of the sandbox hive.
    };

    struct Req
    {
        std::string Root;                        /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string Key;                         /* Key path relative to the root key. Encoding in UTF-8. */
        std::string NewName;                     /* New name of the key, a single component. Encoding in UTF-8. */
        ModeKind    Mode = ModeKind::ReadHandle; /* Layer of the handle, see ModeKind. */
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
 * @brief The tokens of ModeKind, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token opens the key read only.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolRegRenameKey::ModeKind,
                             {
                                 { ProtocolRegRenameKey::ModeKind::ReadHandle, "read_handle" },
                                 { ProtocolRegRenameKey::ModeKind::HiveHandle, "hive_handle" },
})

/**
 * @brief Rename key probe: rename a key of the view and report the view after
 *        the call.
 *
 * A `HiveHandle` request opens the key with a write access, which the isolation
 * copies up into the hive, while a `ReadHandle` request opens it read only,
 * which the read through answers with a handle of the real registry. The rename
 * runs through `NtRenameKey`, which has no Win32 wrapper.
 *
 * The response reports the string values of the old name and of the new name as
 * the view shows them, plus the sub key names of the parent key, so a test can
 * pin which of the two names the merged view holds.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolRegRenameKey::Rsp ProbeRegRenameKey_Entry(const ProtocolRegRenameKey::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGRENAMEKEY_HPP
