#include "DeleteFileW.hpp"
#include "WString.hpp"

using namespace appbox::test;

appbox::test::ProtocolDeleteFileW::Rsp appbox::test::ProbeDeleteFileW_Entry(
    const appbox::test::ProtocolDeleteFileW::Req& request)
{
    const auto& req = request;
    auto        wFileName = appbox::UTF8ToWide(req.FileName);

    ProtocolDeleteFileW::Rsp rsp;
    if (!DeleteFileW(wFileName.c_str()))
    {
        rsp.code = GetLastError();
    }

    return rsp;
}
