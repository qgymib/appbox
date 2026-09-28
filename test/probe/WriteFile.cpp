#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "utils/ReadFileFull.hpp"
#include "WriteFile.hpp"
#include "WString.hpp"

using namespace appbox::test;

static nlohmann::json ProbeWriteFile_Entry(const nlohmann::json& data)
{
    auto req = data.template get<ProtocolWriteFile::Req>();

    const auto wFileName = appbox::UTF8ToWide(req.FileName);

    ProtocolWriteFile::Rsp rsp;

    HANDLE hFile = CreateFileW(wFileName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, req.dwCreationDisposition,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        rsp.code = GetLastError();
        return rsp;
    }

    DWORD written = 0;
    if (!WriteFile(hFile, req.Data.data(), static_cast<DWORD>(req.Data.size()), &written, nullptr) ||
        written != req.Data.size())
    {
        rsp.code = GetLastError();
    }
    else if (!SetEndOfFile(hFile))
    {
        rsp.code = GetLastError();
    }

    CloseHandle(hFile);

    if (rsp.code == 0)
    {
        rsp.code = ReadFileFull(wFileName, rsp.readback);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeWriteFile("WriteFile", ProbeWriteFile_Entry);
