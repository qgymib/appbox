#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "WString.hpp"
#include "CreateFileW.hpp"

using namespace appbox::test;

static nlohmann::json ProbeCreateFileW_Entry(const nlohmann::json& data)
{
    auto req = data.template get<ProtocolCreateFileW::Req>();

    auto wFileName = appbox::UTF8ToWide(req.FileName);
    auto hFile = CreateFileW(wFileName.c_str(), req.dwDesiredAccess, req.dwShareMode, nullptr,
                             req.dwCreationDisposition, req.dwFlagsAndAttributes, nullptr);

    ProtocolCreateFileW::Rsp rsp;
    if (hFile == INVALID_HANDLE_VALUE)
    {
        rsp.code = GetLastError();
    }

    CloseHandle(hFile);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeCreateFileW("CreateFileW", ProbeCreateFileW_Entry);
