#include "CreateDirectoryW.hpp"
#include "WString.hpp"

using namespace appbox::test;

appbox::test::ProtocolCreateDirectoryW::Rsp appbox::test::ProbeCreateDirectoryW_Entry(
    const appbox::test::ProtocolCreateDirectoryW::Req& request)
{
    const auto& req = request;
    auto        wPathName = appbox::UTF8ToWide(req.PathName);

    ProtocolCreateDirectoryW::Rsp rsp;
    if (!CreateDirectoryW(wPathName.c_str(), nullptr))
    {
        rsp.code = GetLastError();
    }

    return rsp;
}
