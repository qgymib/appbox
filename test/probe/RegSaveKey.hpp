#ifndef APPBOX_TEST_PROBE_REGSAVEKEY_HPP
#define APPBOX_TEST_PROBE_REGSAVEKEY_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Protocol of the save key probe.
 *
 * The probe saves a key of the sandbox view into a file and inspects the bytes
 * of that file, so a test can verify what the save exported.
 */
struct ProtocolRegSaveKey
{
    struct Req
    {
        std::string              Root;   /* Root key name, empty means HKEY_CURRENT_USER. */
        std::string              Key;    /* Key path relative to the root key. Encoding in UTF-8. */
        std::string              Path;   /* Absolute DOS path of the file to write. Encoding in UTF-8. */
        std::vector<std::string> Expect; /* Texts the saved hive has to hold. Encoding in UTF-8. */
        std::vector<std::string> Reject; /* Texts the saved hive must not hold. Encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Path, Expect, Reject)
    };

    struct Rsp
    {
        DWORD                    privilege_code = static_cast<DWORD>(-1); /* Enable of SeBackupPrivilege. */
        DWORD                    open_code = static_cast<DWORD>(-1);      /* Open of the key. */
        DWORD                    save_code = static_cast<DWORD>(-1);      /* RegSaveKeyW(). */
        DWORD                    size = 0;                                /* Size of the written file. */
        bool                     hive_signature = false;                  /* The file starts with `regf`. */
        std::vector<std::string> missing;                                 /* Expected texts which are absent. */
        std::vector<std::string> unexpected;                              /* Rejected texts which are present. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, privilege_code, open_code, save_code, size, hive_signature,
                                                    missing, unexpected)
    };
};

/**
 * @brief Save key probe: save a key of the view and inspect the written file.
 *
 * `RegSaveKeyW` requires the `SeBackupPrivilege` of the calling process, so the
 * probe enables it before the call, exactly like an application which saves a
 * hive has to. The written file holds the merged view of the key: the entries
 * of the sandbox hive plus the host entries which the isolation mode keeps
 * visible.
 *
 * The file is read back through its view path and its bytes are searched for
 * the texts of the request. A mount of the file is deliberately not used: the
 * mount of a hive file by path resolves the path outside the isolation view,
 * so it would read a different file than the one the save wrote.
 *
 * The texts are the names and the `REG_SZ` data of the saved key; a standard
 * format hive stores the names as single byte text and the data as UTF-16
 * without compression, so both appear verbatim in the bytes of the file.
 */
extern Probe ProbeRegSaveKey;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGSAVEKEY_HPP
