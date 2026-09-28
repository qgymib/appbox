#include "RemoveDirectory.hpp"
#include "WString.hpp"

using namespace appbox::test;

static nlohmann::json ProbeRemoveDirectory_Entry(const nlohmann::json& data)
{
    auto req = data.template get<ProtocolRemoveDirectory::Req>();
    auto wPathName = appbox::UTF8ToWide(req.PathName);

    ProtocolRemoveDirectory::Rsp rsp;
    if (!RemoveDirectoryW(wPathName.c_str()))
    {
        rsp.code = GetLastError();
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRemoveDirectory("RemoveDirectory", ProbeRemoveDirectory_Entry);
