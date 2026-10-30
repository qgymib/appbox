#include "WinAPI.h" /* Must be first include file */
#include "WString.hpp"
#include "FindStreams.hpp"

using namespace appbox::test;

appbox::test::ProtocolFindStreams::Rsp appbox::test::ProbeFindStreams_Entry(
    const appbox::test::ProtocolFindStreams::Req& request)
{
    const auto& req = request;

    auto wFileName = appbox::UTF8ToWide(req.FileName);

    ProtocolFindStreams::Rsp rsp;
    WIN32_FIND_STREAM_DATA   stream = {};
    HANDLE                   find = FindFirstStreamW(wFileName.c_str(), FindStreamInfoStandard, &stream, 0);
    if (find == INVALID_HANDLE_VALUE)
    {
        rsp.code = GetLastError();
        return rsp;
    }

    /* The enumeration ends with the failure which follows the last stream. */
    for (;;)
    {
        rsp.names.push_back(appbox::WideToUTF8(stream.cStreamName));
        if (!FindNextStreamW(find, &stream))
        {
            break;
        }
    }

    FindClose(find);
    return rsp;
}
