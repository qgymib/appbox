#ifndef APPBOX_TEST_PROBE_FIND_STREAMS_HPP
#define APPBOX_TEST_PROBE_FIND_STREAMS_HPP

#include "WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolFindStreams
{
    static constexpr const char* Method = "FindStreams";

    struct Req
    {
        /** Path of the file to enumerate, in UTF-8. */
        std::string FileName;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName)
    };

    struct Rsp
    {
        /** Error code of the enumeration, zero on success. */
        DWORD code = 0;

        /** Names of the streams the enumeration reported, in UTF-8. */
        std::vector<std::string> names;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, names)
    };
};

/**
 * @brief `FindFirstStreamW` probe.
 *
 * The probe reports every stream of a file, so a case pins which streams the
 * view shows for a file whose marker stream it holds.
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolFindStreams::Rsp ProbeFindStreams_Entry(const ProtocolFindStreams::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_FIND_STREAMS_HPP
