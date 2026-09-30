#ifndef APPBOX_TEST_PROBE_REGREADVALUES_HPP
#define APPBOX_TEST_PROBE_REGREADVALUES_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief Several values of one key, as the sandboxed application reads them.
 *
 * The probe answers every value of one call, so a case which pins the values
 * of a key pays for the chain of the launcher and of the sandbox once. The raw
 * bytes of a value are reported as well, because the types which carry no text
 * (`REG_DWORD`, `REG_BINARY`, ...) can only be pinned that way, and a
 * `REG_MULTI_SZ` value is reported as the list it is instead of one text.
 */
struct ProtocolRegReadValues
{
    /**
     * @brief The question of one probe call.
     */
    struct Req
    {
        /**
         * @brief Root key name, for example `HKEY_LOCAL_MACHINE`.
         *
         * Empty means `HKEY_CURRENT_USER`.
         */
        std::string Root;

        /**
         * @brief Key path relative to the root key, encoding in UTF-8.
         */
        std::string Key;

        /**
         * @brief Names of the values to read, encoding in UTF-8.
         *
         * An empty name addresses the default value of the key.
         */
        std::vector<std::string> Values;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, Root, Key, Values)
    };

    /**
     * @brief The answer for one value.
     */
    struct Answer
    {
        /**
         * @brief Error code of the open of the key, repeated for every value.
         */
        DWORD open_code = static_cast<DWORD>(-1);

        /**
         * @brief Error code of the read of the value.
         */
        DWORD query_code = static_cast<DWORD>(-1);

        /**
         * @brief Type of the value.
         */
        DWORD type = 0;

        /**
         * @brief Data of the value up to its first null character, in UTF-8.
         *
         * Empty for a type which carries no text.
         */
        std::string text;

        /**
         * @brief Parts of the data which null characters separate, each in
         *        UTF-8.
         *
         * A `REG_MULTI_SZ` value is the list of its items; the terminator of
         * the list and a trailing empty item are not part of it. Empty for a
         * type which carries no text.
         */
        std::vector<std::string> items;

        /**
         * @brief Data of the value as a lower case hexadecimal text, without
         *        separators.
         */
        std::string bytes;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Answer, open_code, query_code, type, text, items, bytes)
    };

    /**
     * @brief The answer of one probe call.
     */
    struct Rsp
    {
        /**
         * @brief The answers, in the order of the requested names.
         */
        std::vector<Answer> values;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, values)
    };
};

/**
 * @brief Read several values of one key of the sandbox view.
 */
extern Probe ProbeRegReadValues;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REGREADVALUES_HPP
