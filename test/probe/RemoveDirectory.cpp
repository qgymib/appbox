#include "RemoveDirectory.hpp"
#include "WString.hpp"

using namespace appbox::test;

appbox::test::ProtocolRemoveDirectory::Rsp appbox::test::ProbeRemoveDirectory_Entry(
    const appbox::test::ProtocolRemoveDirectory::Req& request)
{
    const auto& req = request;
    auto        wPathName = appbox::UTF8ToWide(req.PathName).value_or(std::wstring());

    ProtocolRemoveDirectory::Rsp rsp;
    if (!RemoveDirectoryW(wPathName.c_str()))
    {
        rsp.code = GetLastError();
    }

    return rsp;
}
