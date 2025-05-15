#include <Windows.h>
#include "wstring.hpp"

std::wstring appbox::mbstowcs(const char* s, int codepage)
{
    return appbox::mbstowcs(s, (size_t)-1, codepage);
}

std::wstring appbox::mbstowcs(const char* s, size_t n, int codepage)
{
    int          size_need = MultiByteToWideChar(codepage, 0, s, (int)n, nullptr, 0);
    std::wstring dst(size_need, 0);
    MultiByteToWideChar(codepage, 0, s, (int)n, &dst[0], size_need);
    return dst;
}

std::string appbox::wcstombs(const wchar_t* s, int codepage)
{
    return appbox::wcstombs(s, (size_t)-1, codepage);
}

std::string appbox::wcstombs(const wchar_t* s, size_t n, int codepage)
{
    int size_need = WideCharToMultiByte(codepage, 0, s, (int)n, nullptr, 0, nullptr, nullptr);
    std::string dst(size_need, 0);
    WideCharToMultiByte(codepage, 0, s, (int)n, &dst[0], size_need, nullptr, nullptr);
    return dst;
}
