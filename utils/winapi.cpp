#include "winapi.hpp"

std::wstring appbox::exepath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

uint64_t appbox::GetFileSize(HANDLE hFile)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        throw std::exception("GetFileSizeEx failed");
    }
    return fileSize.QuadPart;
}
