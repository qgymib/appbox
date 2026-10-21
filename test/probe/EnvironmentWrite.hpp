#ifndef APPBOX_TEST_PROBE_ENVIRONMENT_WRITE_HPP
#define APPBOX_TEST_PROBE_ENVIRONMENT_WRITE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief The modifications a probe makes to the environment of the sandbox.
 *
 * The probe stores and removes variables through one entry point of the
 * environment of a process and reads them back afterwards, so one call pins
 * both the answer of a write and the view the write produced.
 *
 * The environment of a process has more than one writer, and the isolation has
 * to observe every one of them: two Win32 entry points, the counted entry point
 * of `ntdll` with the process environment and with an explicit block, and two
 * entry points which replace the whole block.
 */
struct ProtocolEnvironmentWrite
{
    /**
     * @brief Entry point the probe writes through.
     *
     * A case selects the entry point it pins instead of relying on one of them.
     *
     * - `Wide` - `SetEnvironmentVariableW`, the default.
     * - `Ansi` - `SetEnvironmentVariableA`.
     * - `RtlNull` - `RtlSetEnvironmentVariable` with a null block, which is how
     *   the runtime of the operating system asks for the environment of the
     *   process.
     * - `RtlBlock` - `RtlSetEnvironmentVariable` with the address of the
     *   environment pointer of the process parameters, which the counted entry
     *   point treats like the null block.
     * - `Strings` - `SetEnvironmentStringsW`, which replaces the whole block
     *   with a block the probe builds out of the current one.
     * - `Current` - `RtlSetCurrentEnvironment`, which installs a block the
     *   probe builds out of the current one.
     */
    enum class Entry
    {
        Wide,     ///< `SetEnvironmentVariableW`, the default.
        Ansi,     ///< `SetEnvironmentVariableA`.
        RtlNull,  ///< `RtlSetEnvironmentVariable` with a null block.
        RtlBlock, ///< `RtlSetEnvironmentVariable` with the block of the parameters.
        Strings,  ///< `SetEnvironmentStringsW`, replaces the whole block.
        Current   ///< `RtlSetCurrentEnvironment`, replaces the whole block.
    };

    /**
     * @brief The modifications of one probe call.
     */
    struct Req
    {
        /**
         * @brief Names of the variables to store, encoding in UTF-8.
         */
        std::vector<std::string> names;

        /**
         * @brief Values to store, one per name, encoding in UTF-8.
         */
        std::vector<std::string> values;

        /**
         * @brief Names of the variables to remove, encoding in UTF-8.
         */
        std::vector<std::string> remove;

        /**
         * @brief Names of the variables to read back, encoding in UTF-8.
         */
        std::vector<std::string> read;

        /**
         * @brief Entry point the probe writes through, see Entry.
         */
        Entry entry = Entry::Wide;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, names, values, remove, read, entry)
    };

    /**
     * @brief The answer of one probe call.
     */
    struct Rsp
    {
        /**
         * @brief Whether the entry point stored every variable.
         */
        std::vector<bool> stored;

        /**
         * @brief Whether the entry point removed every variable.
         */
        std::vector<bool> removed;

        /**
         * @brief Whether the environment holds every variable which was read
         *        back.
         */
        std::vector<bool> found;

        /**
         * @brief Value of every variable which was read back.
         */
        std::vector<std::string> values;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, stored, removed, found, values)
    };
};

/**
 * @brief The tokens of Entry, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token writes through `SetEnvironmentVariableW`.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolEnvironmentWrite::Entry,
                             {
                                 { ProtocolEnvironmentWrite::Entry::Wide,     "wide"      },
                                 { ProtocolEnvironmentWrite::Entry::Ansi,     "ansi"      },
                                 { ProtocolEnvironmentWrite::Entry::RtlNull,  "rtl_null"  },
                                 { ProtocolEnvironmentWrite::Entry::RtlBlock, "rtl_block" },
                                 { ProtocolEnvironmentWrite::Entry::Strings,  "strings"   },
                                 { ProtocolEnvironmentWrite::Entry::Current,  "current"   },
})

/**
 * @brief Modify the environment of the sandboxed application.
 */
extern Probe ProbeEnvironmentWrite;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_ENVIRONMENT_WRITE_HPP
