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
 * The probe stores and removes variables with the wide entry point of the
 * process environment and reads them back afterwards, so one call pins both the
 * answer of a write and the view the write produced.
 */
struct ProtocolEnvironmentWrite
{
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

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, names, values, remove, read)
    };

    /**
     * @brief The answer of one probe call.
     */
    struct Rsp
    {
        /**
         * @brief Whether the wide entry point stored every variable.
         */
        std::vector<bool> stored;

        /**
         * @brief Whether the wide entry point removed every variable.
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
 * @brief Modify the environment of the sandboxed application.
 */
extern Probe ProbeEnvironmentWrite;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_ENVIRONMENT_WRITE_HPP
