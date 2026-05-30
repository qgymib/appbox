#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "utils/ReadFileFull.hpp"
#include "ReadFileFull.hpp"
#include "WString.hpp"

static nlohmann::json ProbeReadFile_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolReadFileFull::Req>();

    appbox::test::ProtocolReadFileFull::Rsp rsp;
    rsp.code = appbox::test::ReadFileFull(appbox::UTF8ToWide(req.FileName), rsp.data);

    return rsp;
}

appbox::test::Probe appbox::test::ProbeReadFileFull("ReadFile", ProbeReadFile_Entry);
