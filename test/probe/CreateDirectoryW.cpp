#include "CreateDirectoryW.hpp"
#include "WString.hpp"

using namespace appbox::test;

static nlohmann::json ProbeCreateDirectoryW_Entry(const nlohmann::json& data)
{
    auto req = data.template get<ProtocolCreateDirectoryW::Req>();
    auto wPathName = appbox::UTF8ToWide(req.PathName);

    ProtocolCreateDirectoryW::Rsp rsp;
    if (CreateDirectoryW(wPathName.c_str(), nullptr))
    {
        rsp.code = GetLastError();
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeCreateDirectoryW("CreateDirectoryW", ProbeCreateDirectoryW_Entry);
