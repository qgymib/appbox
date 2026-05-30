#include "WriteFileFull.hpp"

DWORD appbox::test::WriteFileFull(const std::wstring& FileName, const std::vector<uint8_t>& data)
{
    auto hFile = CreateFileW(FileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    auto wr = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), nullptr, nullptr);
    CloseHandle(hFile);

    return wr ? 0 : GetLastError();
}

DWORD appbox::test::WriteFileFull(const std::wstring& FileName, const std::string& data)
{
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return WriteFileFull(FileName, bytes);
}
