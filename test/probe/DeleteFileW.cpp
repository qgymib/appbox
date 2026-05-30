#include "DeleteFileW.hpp"
#include "WString.hpp"

using namespace appbox::test;

static nlohmann::json ProbeDeleteFileW_Entry(const nlohmann::json& data)
{
    auto req = data.template get<ProtocolDeleteFileW::Req>();
    auto wFileName = appbox::UTF8ToWide(req.FileName);

    ProtocolDeleteFileW::Rsp rsp;
    if (!DeleteFileW(wFileName.c_str()))
    {
        rsp.code = GetLastError();
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeDeleteFileW("DeleteFileW", ProbeDeleteFileW_Entry);
