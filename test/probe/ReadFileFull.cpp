#include "WinAPI.h" /* Must be first include file */
#include "utils/ReadFileFull.hpp"
#include "ReadFileFull.hpp"
#include "WString.hpp"

appbox::test::ProtocolReadFileFull::Rsp appbox::test::ProbeReadFileFull_Entry(
    const appbox::test::ProtocolReadFileFull::Req& request)
{
    const auto& req = request;

    appbox::test::ProtocolReadFileFull::Rsp rsp;
    rsp.code = appbox::test::ReadFileFull(appbox::UTF8ToWide(req.FileName), rsp.data);

    return rsp;
}
