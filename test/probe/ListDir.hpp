#ifndef APPBOX_TEST_PROBE_LIST_DIR_HPP
#define APPBOX_TEST_PROBE_LIST_DIR_HPP

#include "WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolListDir
{
    static constexpr const char* Method = "ListDir";

    struct Req
    {
        enum class Method
        {
            Std,    /* std::filesystem::directory_iterator */
            WinAPI, /* FindFirstFile */
            CRT,    /* _findfirst */
        };

        std::string path;   /* Directory path. */
        Method      method; /* Method */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, path, method)
    };

    struct Rsp
    {
        struct Entry
        {
            std::string name; /* Entry name */
            bool        file; /* Is file */
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(Entry, name, file)
        };

        std::vector<Entry> entries;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, entries)
    };
};

/**
 * @brief List directory entries.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolListDir::Rsp ProbeListDir_Entry(const ProtocolListDir::Req& request);

} // namespace appbox::test

#endif
