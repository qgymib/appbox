#include "WinAPI.h" /* Must be first include file */
#include "WString.hpp"
#include "CreateFileW.hpp"

using namespace appbox::test;

appbox::test::ProtocolCreateFileW::Rsp appbox::test::ProbeCreateFileW_Entry(
    const appbox::test::ProtocolCreateFileW::Req& request)
{
    const auto& req = request;

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
