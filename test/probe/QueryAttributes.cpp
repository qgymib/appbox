#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "QueryAttributes.hpp"
#include "WString.hpp"

static nlohmann::json ProbeQueryAttributes_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolQueryAttributes::Req>();

    appbox::test::ProtocolQueryAttributes::Rsp rsp;
    rsp.attributes = GetFileAttributesW(appbox::UTF8ToWide(req.FileName).c_str());
    rsp.code = (rsp.attributes == INVALID_FILE_ATTRIBUTES) ? GetLastError() : 0;

    return rsp;
}

appbox::test::Probe appbox::test::ProbeQueryAttributes("QueryAttributes", ProbeQueryAttributes_Entry);
