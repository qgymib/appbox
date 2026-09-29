#ifndef APPBOX_TEST_PROBE_ENVIRONMENT_READ_HPP
#define APPBOX_TEST_PROBE_ENVIRONMENT_READ_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief The environment of the sandboxed application, as the probe sees it.
 *
 * The probe answers with every entry point of the environment of a process, so
 * one case pins the whole view of the sandbox: the value of a variable, the
 * entries of the block which enumerates the environment, and the expansion of
 * the references of a text.
 */
struct ProtocolEnvironmentRead
{
    /**
     * @brief The question of one probe call.
     */
    struct Req
    {
        /**
         * @brief Names of the variables to read, encoding in UTF-8.
         */
        std::vector<std::string> names;

        /**
         * @brief Texts whose `%NAME%` references are expanded, encoding in
         *        UTF-8.
         */
        std::vector<std::string> expand;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, names, expand)
    };

    /**
     * @brief The answer of one probe call.
     */
    struct Rsp
    {
        /**
         * @brief Value of every name, read with the wide entry point.
         */
        std::vector<std::string> values;

        /**
         * @brief Whether the wide entry point found the name.
         *
         * A variable which the environment holds with an empty value is found
         * as well, so a case can tell the two apart.
         */
        std::vector<bool> found;

        /**
         * @brief Value of every name, read with the ANSI entry point.
         */
        std::vector<std::string> ansi_values;

        /**
         * @brief The expansion of every text, wide and ANSI.
         */
        std::vector<std::string> expanded;

        /**
         * @brief The ANSI expansion of every text.
         */
        std::vector<std::string> ansi_expanded;

        /**
         * @brief Entries of the block which enumerates the environment, wide
         *        and ANSI.
         */
        std::vector<std::string> entries;

        /**
         * @brief Entries of the ANSI block which enumerates the environment.
         */
        std::vector<std::string> ansi_entries;

        /**
         * @brief Status the lowest reader of the process environment reported
         *        for a size query which brings no value buffer at all.
         */
        std::uint32_t rtl_size_query_status = 0;

        /**
         * @brief Length the lowest reader reported for that query.
         */
        std::uint32_t rtl_size_query_length = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, values, found, ansi_values, expanded, ansi_expanded, entries, ansi_entries,
                                       rtl_size_query_status, rtl_size_query_length)
    };
};

/**
 * @brief Read the environment of the sandboxed application.
 */
extern Probe ProbeEnvironmentRead;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_ENVIRONMENT_READ_HPP
