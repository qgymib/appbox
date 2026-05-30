#include "ReadFileFull.hpp"

DWORD appbox::test::ReadFileFull(const std::wstring& path, std::string& data)
{
    std::vector<uint8_t> bytes;
    DWORD                result = ReadFileFull(path, bytes);
    if (result != ERROR_SUCCESS)
    {
        return result;
    }

    data = std::string(bytes.begin(), bytes.end());
    return ERROR_SUCCESS;
}

DWORD appbox::test::ReadFileFull(const std::wstring& path, std::vector<uint8_t>& bytes)
{
    auto hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        return GetLastError();
    }

    bytes.clear();
    bytes.resize(fileSize.QuadPart);
    auto rd = ReadFile(hFile, bytes.data(), static_cast<DWORD>(fileSize.QuadPart), nullptr, nullptr);
    CloseHandle(hFile);

    return rd ? 0 : GetLastError();
}
