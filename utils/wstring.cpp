#include <Windows.h>
#include "wstring.hpp"

std::wstring appbox::mbstowcs(const char* s)
{
    return appbox::mbstowcs(s, strlen(s));
}

std::wstring appbox::mbstowcs(const char* s, size_t n)
{
    int          size_need = MultiByteToWideChar(CP_ACP, 0, s, (int)n, nullptr, 0);
    std::wstring dst(size_need, 0);
    MultiByteToWideChar(CP_ACP, 0, s, (int)n, &dst[0], size_need);
    return dst;
}
