#include "WinAPI.h" /* Must be first include file */
#include "QueryAttributes.hpp"
#include "WString.hpp"

appbox::test::ProtocolQueryAttributes::Rsp appbox::test::ProbeQueryAttributes_Entry(
    const appbox::test::ProtocolQueryAttributes::Req& request)
{
    const auto& req = request;

    appbox::test::ProtocolQueryAttributes::Rsp rsp;
    const std::wstring                         path = appbox::UTF8ToWide(req.FileName).value_or(std::wstring());
    rsp.attributes = GetFileAttributesW(path.c_str());
    rsp.code = (rsp.attributes == INVALID_FILE_ATTRIBUTES) ? GetLastError() : 0;

    return rsp;
}
